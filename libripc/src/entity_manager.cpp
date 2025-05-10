#include "ripc/entity_manager.hpp"
#include "ripc/server.hpp"
#include "ripc/client.hpp"
#include "ripc/context.hpp" // Для вызова методов контекста
#include "ripc.h"           // Для IOCTL и notification_data
#include "id_pack.h"        // Для IS_ID_VALID
#include <iostream>
#include <algorithm> // std::find_if
#include <poll.h>
#include <unistd.h>     // read, close
#include <system_error> // std::system_error для потока
#include <vector>       // Для временного буфера
#include <cstring>      // strerror

namespace ripc
{

    // --- Синглтон ---
    RipcEntityManager &RipcEntityManager::getInstance()
    {
        // Потокобезопасно в C++11+
        static RipcEntityManager instance;
        // НЕ проверяем is_initialized здесь, чтобы shutdown мог работать
        return instance;
    }

    // --- Инициализация / Завершение ---
    void RipcEntityManager::doInitialize(const std::string &device_path)
    {
        std::lock_guard<std::mutex> lock(manager_mutex); // Защищаем весь процесс инициализации
        if (is_initialized)
        {
            throw std::logic_error("RipcEntityManager already initialized.");
        }
        std::cout << "EntityManager: Initializing..." << std::endl;
        context = std::unique_ptr<RipcContext>(new RipcContext());
        try
        {
            context->openDevice(device_path); // Может бросить исключение
        }
        catch (...)
        {
            context.reset(); // Очистка контекста при ошибке
            std::cerr << "EntityManager: Context initialization failed." << std::endl;
            throw;
        }

        // Запуск потока слушателя
        listener_running.store(true);
        try
        {
            listener_thread = std::thread(&RipcEntityManager::notificationListenerLoop, this);
        }
        catch (const std::system_error &e)
        {
            listener_running.store(false);
            context.reset();        // Закрыть устройство
            is_initialized = false; // Сбросить флаг
            std::cerr << "EntityManager: Failed to create listener thread." << std::endl;
            throw std::runtime_error("Failed to create listener thread: " + std::string(e.what()));
        }

        is_initialized = true; // Инициализация завершена успешно
        std::cout << "EntityManager: Initialization complete. Listener thread started." << std::endl;
    }

    void RipcEntityManager::doShutdown()
    {
        // 1. Остановить поток (вне блокировки менеджера)
        if (listener_running.exchange(false))
        { // Потокобезопасно устанавливаем false и проверяем старое значение
            if (listener_thread.joinable())
            {
                std::cout << "EntityManager: Waiting for listener thread to join..." << std::endl;
                try
                {
                    listener_thread.join();
                    std::cout << "EntityManager: Listener thread joined." << std::endl;
                }
                catch (const std::system_error &e)
                {
                    std::cerr << "EntityManager: Error joining listener thread: " << e.what() << std::endl;
                }
            }
        }

        // 2. Блокируем менеджер для очистки остального
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
        {
            // std::cout << "EntityManager: Shutdown called but already not initialized." << std::endl;
            return; // Уже не инициализирован
        }
        std::cout << "EntityManager: Shutting down..." << std::endl;

        // Очистка карт вызовет деструкторы unique_ptr -> деструкторы Server/Client
        std::cout << "EntityManager: Clearing clients (" << clients.size() << ")..." << std::endl;
        clients.clear();
        std::cout << "EntityManager: Clearing servers (" << servers.size() << ")..." << std::endl;
        servers.clear();
        notification_handlers.clear();

        // Контекст будет очищен автоматически деструктором unique_ptr context
        std::cout << "EntityManager: Resetting context..." << std::endl;
        context.reset(); // Явно вызываем деструктор контекста (закроет fd)

        is_initialized = false;
        std::cout << "EntityManager: Shutdown finished." << std::endl;
    }

    RipcContext &RipcEntityManager::getContext()
    {
        // Быстрая проверка без блокировки (оптимизация, но требует careful consideration с memory order)
        // if (!is_initialized || !context) {
        //      throw std::logic_error("RipcEntityManager or Context not initialized.");
        // }
        // Более безопасный вариант - всегда под блокировкой для чтения, т.к. инициализация/очистка меняют context
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized || !context)
        {
            throw std::logic_error("RipcEntityManager or Context not initialized.");
        }
        return *context;
    }

    // --- Фабрики и Управление (с использованием unordered_map) ---
    Server *RipcEntityManager::createServer(const std::string &name)
    {
        if (!is_initialized)
            throw std::logic_error("Manager not initialized.");
        if (servers.size() >= max_servers)
            throw std::runtime_error("Server limit reached.");

        auto new_server = std::unique_ptr<Server>(new Server(getContext(), name));
        try
        {
            new_server->init(); // Выполняет ioctl register
        }
        catch (...)
        {
            throw; // Перебрасываем исключение, unique_ptr удалит объект
        }

        int new_id = new_server->getId();
        if (!IS_ID_VALID(new_id))
        {
            // Это не должно произойти, если init() отработал без исключений
            throw std::logic_error("Server init returned invalid ID after success.");
        }
        // Проверка на коллизию ID (маловероятно, но важно)
        if (servers.count(new_id))
        {
            // Что делать? Отменить регистрацию в ядре? Сложно. Пока бросаем исключение.
            // TODO: Возможно, добавить ioctl для отмены регистрации при ошибке здесь.
            throw std::logic_error("Server ID collision detected: " + std::to_string(new_id));
        }

        Server *raw_ptr = new_server.get();
        // Вставляем в unordered_map, перемещая владение unique_ptr
        std::lock_guard<std::mutex> lock(manager_mutex);
        servers.emplace(new_id, std::move(new_server));
        std::cout << "EntityManager: Server '" << name << "' (ID: " << new_id << ") created." << std::endl;
        return raw_ptr;
    }

    Client *RipcEntityManager::createClient()
    {
        if (!is_initialized)
            throw std::logic_error("Manager not initialized.");
        if (clients.size() >= max_clients)
            throw std::runtime_error("Client limit reached.");

        auto new_client = std::unique_ptr<Client>(new Client(getContext()));
        try
        {
            new_client->init(); // Выполняет ioctl register
        }
        catch (...)
        {
            throw;
        }

        int new_id = new_client->getId();
        if (!IS_ID_VALID(new_id))
        {
            throw std::logic_error("Client init returned invalid ID after success.");
        }
        if (clients.count(new_id))
        {
            // TODO: Отмена регистрации клиента при коллизии?
            throw std::logic_error("Client ID collision detected: " + std::to_string(new_id));
        }

        Client *raw_ptr = new_client.get();
        std::lock_guard<std::mutex> lock(manager_mutex);
        clients.emplace(new_id, std::move(new_client));
        std::cout << "EntityManager: Client (ID: " << new_id << ") created." << std::endl;
        return raw_ptr;
    }

    bool RipcEntityManager::deleteServer(int server_id)
    {
        if (!IS_ID_VALID(server_id))
            return false;
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
            return false;

        // Метод erase для unordered_map возвращает количество удаленных элементов (0 или 1)
        size_t removed_count = servers.erase(server_id);

        if (removed_count > 0)
        {
            std::cout << "EntityManager: Server ID " << server_id << " deleted." << std::endl;
            return true;
        }
        else
        {
            std::cerr << "EntityManager: Server ID " << server_id << " not found for deletion." << std::endl; // Можно не выводить
            return false;
        }
        // Деструктор unique_ptr вызывается автоматически при удалении элемента из карты
    }

    bool RipcEntityManager::deleteServer(Server *server)
    {
        if (!server)
            return false;
        return deleteServer(server->m_server_id);
    }

    bool RipcEntityManager::deleteClient(int client_id)
    {
        if (!IS_ID_VALID(client_id))
            return false;
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
            return false;

        size_t removed_count = clients.erase(client_id);

        if (removed_count > 0)
        {
            std::cout << "EntityManager: Client ID " << client_id << " deleted." << std::endl;
            return true;
        }
        else
        {
            std::cerr << "EntityManager: Client ID " << client_id << " not found for deletion." << std::endl;
            return false;
        }
    }

    bool RipcEntityManager::deleteClient(Client *client)
    {
        if (!client)
            return false;
        return deleteClient(client->m_client_id);
    }

    // Поиск O(1) в среднем для unordered_map
    Server *RipcEntityManager::findServerById(int server_id)
    {
        if (!IS_ID_VALID(server_id))
            return nullptr;
        // Блокировка нужна для безопасного доступа к карте
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
            return nullptr;

        auto it = servers.find(server_id);
        return (it != servers.end()) ? it->second.get() : nullptr;
    }

    Client *RipcEntityManager::findClientById(int client_id)
    {
        if (!IS_ID_VALID(client_id))
            return nullptr;
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
            return nullptr;

        auto it = clients.find(client_id);
        return (it != clients.end()) ? it->second.get() : nullptr;
    }

    // --- Поток и Диспетчеризация ---
    void RipcEntityManager::registerHandler(enum notif_type type, NotificationHandler handler)
    {
        if (!IS_NTF_TYPE_VALID(type))
        {
            throw std::invalid_argument("Invalid notification type for handler registration.");
        }
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (handler)
        {
            notification_handlers[type] = std::move(handler);
            std::cout << "EntityManager: Registered custom handler for type " << type << "." << std::endl;
        }
        else
        {
            notification_handlers.erase(type);
            std::cout << "EntityManager: Unregistered custom handler for type " << type << "." << std::endl;
        }
    }

    void RipcEntityManager::dispatchNotification(const notification_data &ntf)
    {
        // Базовые проверки валидности
        if (!IS_NTF_TYPE_VALID(ntf.m_type) || !IS_NTF_SEND_VALID(ntf.m_who_sends) || !IS_ID_VALID(ntf.m_reciver_id))
        {
            std::cerr << "Dispatcher: Invalid notification received (type=" << ntf.m_type
                      << ", sender=" << ntf.m_who_sends << ", receiver=" << ntf.m_reciver_id << ")" << std::endl;
            return;
        }

        NotificationHandler custom_handler = nullptr;
        Server *target_server = nullptr;
        Client *target_client = nullptr;
        enum notif_type current_type = static_cast<enum notif_type>(ntf.m_type);
        int receiver_id = ntf.m_reciver_id; // Копируем ID получателя

        { // Блок для lock_guard
            std::lock_guard<std::mutex> lock(manager_mutex);
            if (!is_initialized)
                return; // Проверка под блокировкой

            // 1. Ищем пользовательский обработчик
            auto it_handler = notification_handlers.find(current_type);
            if (it_handler != notification_handlers.end())
            {
                custom_handler = it_handler->second; // Копируем std::function
            }

            // 2. Если нет, ищем целевой объект (используем find под той же блокировкой)
            if (!custom_handler)
            {
                if (ntf.m_who_sends == CLIENT)
                { // К серверу
                    std::cout << "RipcEntityManager::dispatchNotification: send notification to server\n";
                    auto it_srv = servers.find(receiver_id);
                    if (it_srv != servers.end())
                        target_server = it_srv->second.get();
                }
                else if (ntf.m_who_sends == SERVER)
                { // К клиенту
                    std::cout << "RipcEntityManager::dispatchNotification: send notification to client\n";
                    auto it_cli = clients.find(receiver_id);
                    if (it_cli != clients.end())
                        target_client = it_cli->second.get();
                }
                // Указатели target_server/target_client валидны только пока держится мьютекс,
                // если объекты могут быть удалены другим потоком.
                // НО! Вызов handleNotification вне мьютекса безопасен, т.к. сам объект
                // еще существует (unique_ptr в карте жив).
            }
        } // Конец блока lock_guard

        // 3. Вызов обработчика/метода объекта вне блокировки
        try
        {
            if (custom_handler)
            {
                custom_handler(ntf);
            }
            else if (target_server)
            {
                target_server->handleNotification(ntf);
            }
            else if (target_client)
            {
                target_client->handleNotification(ntf);
            }
            else
            {
                // Объект не найден, и нет пользовательского обработчика
                std::cout << "Dispatcher: No handler or target instance found for notification type "
                          << ntf.m_type << " to receiver " << receiver_id << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Dispatcher: Exception during notification handling: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Dispatcher: Unknown exception during notification handling." << std::endl;
        }
    }

    void RipcEntityManager::notificationListenerLoop()
    {
        std::cout << "[Listener Thread " << std::this_thread::get_id() << "]: Started." << std::endl;
        int local_fd = -1;

        try
        {
            // Получаем fd один раз (getContext безопасно читает под мьютексом)
            local_fd = getContext().getFd();
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Listener Thread]: Failed to get device fd: " << e.what() << ". Stopping." << std::endl;
            listener_running.store(false);
            return;
        }

        pollfd pfd;
        pfd.fd = local_fd;
        pfd.events = POLLIN; // Ждем данные для чтения

        while (listener_running.load())
        {
            pfd.revents = 0;               // Сбрасываем перед poll
            int ret = poll(&pfd, 1, 1000); // Таймаут 1 секунда

            if (!listener_running.load())
                break; // Проверяем флаг после пробуждения

            if (ret < 0)
            { // Ошибка poll
                if (errno == EINTR)
                    continue;
                perror("[Listener Thread]: poll failed");
                listener_running.store(false); // Ошибка, останавливаем поток
                break;
            }
            else if (ret == 0)
            {             // Таймаут
                continue; // Ничего нет
            }

            // Есть событие (ret > 0)
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                std::cerr << "[Listener Thread]: Error/Hangup/Invalid event on fd (revents: 0x"
                          << std::hex << pfd.revents << std::dec << "). Stopping." << std::endl;
                listener_running.store(false);
                break;
            }

            if (pfd.revents & POLLIN)
            {
                // Данные готовы к чтению
                notification_data ntf;
                ssize_t bytes_read;

                // Читаем все доступные уведомления
                while (listener_running.load()) // Проверяем флаг перед каждым read
                {
                    bytes_read = read(local_fd, &ntf, sizeof(ntf));

                    if (bytes_read == sizeof(ntf))
                    {
                        dispatchNotification(ntf); // Диспетчеризуем полное уведомление
                    }
                    else if (bytes_read < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break; // Данных больше нет
                        }
                        else if (errno == EINTR)
                        {
                            continue; // Прервано, повторить read
                        }
                        else
                        {
                            // Ошибка чтения, логируем, но не останавливаем поток
                            perror("[Listener Thread]: read failed");
                            break; // Выходим из цикла чтения
                        }
                    }
                    else
                    { // bytes_read == 0 (EOF?) или прочитано меньше, чем ожидалось
                        std::cerr << "[Listener Thread]: Unexpected read result (" << bytes_read
                                  << ", expected " << sizeof(ntf) << "). Stopping read loop." << std::endl;
                        // Можно попытаться прочитать остаток или просто выйти из цикла read
                        break;
                    }
                } // end read loop
            } // end if POLLIN
        } // end while(listener_running)

        std::cout << "[Listener Thread " << std::this_thread::get_id() << "]: Exiting." << std::endl;
    }

    // --- Установка лимитов (статические методы) ---
    // void RipcEntityManager::setGlobalServerLimit(size_t limit)
    // {
    //     std::lock_guard<std::mutex> lock(getInstance().manager_mutex);
    //     if(getInstance().is_initialized)
    //     {
    //         std::cerr << "Ripc library should not be initialized for changing server limit\n";
    //         return;
    //     }
    //     getInstance().max_servers = limit;
    //     std::cout << "Global server limit set to " << limit << std::endl;
    // }

    // void RipcEntityManager::setGlobalClientLimit(size_t limit)
    // {
    //     std::lock_guard<std::mutex> lock(getInstance().manager_mutex);
    //     if(getInstance().is_initialized)
    //     {
    //         std::cerr << "Ripc library should not be initialized for changing client limit\n";
    //         return;
    //     }
    //     getInstance().max_clients = limit;
    //     std::cout << "Global client limit set to " << limit << std::endl;
    // }

} // namespace ripc