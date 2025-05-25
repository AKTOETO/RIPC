#include "ripc/entity_manager.hpp"
#include "id_pack.h" // Для IS_ID_VALID
#include "ripc.h"    // Для IOCTL и notification_data
#include "ripc/client.hpp"
#include "ripc/context.hpp" // Для вызова методов контекста
#include "ripc/logger.hpp"
#include "ripc/rest_client.hpp"
#include "ripc/rest_server.hpp"
#include "ripc/server.hpp"
#include <algorithm> // std::find_if
#include <cstring>   // strerror
#include <iostream>
#include <memory>
#include <poll.h>
#include <system_error> // std::system_error для потока
#include <unistd.h>     // read, close
#include <vector>       // Для временного буфера

namespace ripc
{
#define CHECK_INIT                                                                                                     \
    {                                                                                                                  \
        if (!isInitialized())                                                                                          \
        {                                                                                                              \
            LOG_ERR("Not initialized");                                                                                \
            return false;                                                                                              \
        }                                                                                                              \
    }
    // --- Синглтон ---
    RipcEntityManager &RipcEntityManager::getInstance()
    {
        static RipcEntityManager instance;
        return instance;
    }

    // --- Инициализация / Завершение ---
    bool RipcEntityManager::doInitialize(const std::string &device_path)
    {
        std::lock_guard<std::mutex> lock(manager_mutex); // Защищаем весь процесс инициализации
        if (is_initialized)
        {
            // throw std::logic_error("RipcEntityManager already initialized.");
            LOG_ERR("RipcEntityManager already initialized");
            return false;
        }

        // std::cout << "EntityManager: Initializing..." << std::endl;
        LOG_INFO("Initializing...");
        context = std::unique_ptr<RipcContext>(new RipcContext());

        if (!context->openDevice(device_path))
            return false;

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
            // std::cerr << "EntityManager: Failed to create listener thread." << std::endl;
            LOG_CRIT("Failed to create listener thread: %s", e.what());
            return false;
            // throw std::runtime_error("Failed to create listener thread: " + std::string(e.what()));
        }

        is_initialized = true; // Инициализация завершена успешно
        // std::cout << "EntityManager: Initialization complete. Listener thread started." << std::endl;
        LOG_INFO("Initialization complete. Listener thread started.");
        return true;
    }

    bool RipcEntityManager::doShutdown()
    {
        // Остановить поток
        if (listener_running.exchange(false))
        { // Потокобезопасно устанавливаем false и проверяем старое значение
            if (listener_thread.joinable())
            {
                // std::cout << "EntityManager: Waiting for listener thread to join..." << std::endl;
                LOG_INFO("Waiting for listener thread to join...");
                try
                {
                    listener_thread.join();
                    // std::cout << "EntityManager: Listener thread joined." << std::endl;
                    LOG_INFO("Listener thread joined");
                }
                catch (const std::system_error &e)
                {
                    // std::cerr << "EntityManager: Error joining listener thread: " << e.what() << std::endl;
                    LOG_WARN("Error joining listener thread: %s", e.what());
                }
            }
        }

        // Блокируем менеджер для очистки остального
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!isInitialized())
            return false;

        LOG_INFO("Shutting down...");
        // std::cout << "EntityManager: Shutting down..." << std::endl;

        // Очистка карт вызовет деструкторы unique_ptr -> деструкторы Server/Client
        // std::cout << "EntityManager: Clearing clients (" << clients.size() << ")..." << std::endl;
        LOG_INFO("Clearing clients (%ld)", clients.size());
        clients.clear();
        // std::cout << "EntityManager: Clearing servers (" << servers.size() << ")..." << std::endl;
        LOG_INFO("Clearing servers (%ld)", servers.size());
        servers.clear();

        LOG_INFO("Clearing notifiction handlers (%ld)", notification_handlers.size());
        notification_handlers.clear();

        // std::cout << "EntityManager: Resetting context" << std::endl;
        LOG_INFO("Resetting context");
        context.reset();

        is_initialized = false;
        // std::cout << "EntityManager: Shutdown finished." << std::endl;
        LOG_INFO("Shutdown finished");
        return true;
    }

    RipcContext &RipcEntityManager::getContext()
    {
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized || !context)
        {
            // throw std::logic_error("RipcEntityManager or Context not initialized.");
            LOG_CRIT("RipcEntityManager or Context not initialized");
        }
        return *context;
    }

    // --- Фабрики и Управление (с использованием unordered_map) ---
    Server *RipcEntityManager::createServer(const std::string &name)
    {
        if (!is_initialized)
        {
            LOG_CRIT("Manager is not initialized");
            return nullptr;
        }
        // throw std::logic_error("Manager not initialized.");
        if (servers.size() >= max_servers)
        {
            LOG_ERR("Server limit reached");
            return nullptr;
        }
        // throw std::runtime_error("Server limit reached.");

        auto new_server =
            std::make_unique<Server>(getContext(), name); // std::unique_ptr<Server>(new Server(getContext(), name));
        if (!new_server->init())
        {
            new_server.reset();
            return nullptr;
        }

        int new_id = new_server->getId();
        if (!IS_ID_VALID(new_id))
        {
            // throw std::logic_error("Server init returned invalid ID after success.");

            // удаляем сервер
            new_server.reset();
            LOG_ERR("Server got invalid id");
            return nullptr;
        }
        // Проверка на коллизию ID (маловероятно, но важно)
        if (servers.count(new_id))
        {
            // throw std::logic_error("Server ID collision detected: " + std::to_string(new_id));
            //  удаляем сервер
            new_server.reset();
            LOG_ERR("Server ID collision detected: %d", new_id);
            return nullptr;
        }

        Server *raw_ptr = new_server.get();
        // Вставляем в unordered_map, перемещая владение unique_ptr
        std::lock_guard<std::mutex> lock(manager_mutex);
        servers.emplace(new_id, std::move(new_server));
        // std::cout << "EntityManager: Server '" << name << "' (ID: " << new_id << ") created." << std::endl;
        LOG_INFO("Server '%s' (ID:%d) created", name.c_str(), new_id);
        return raw_ptr;
    }

    RESTServer *RipcEntityManager::createRestfulServer(const std::string &name)
    {
        if (!is_initialized)
        {
            LOG_CRIT("Manager is not initialized");
            return nullptr;
        }
        // throw std::logic_error("Manager not initialized.");
        if (servers.size() >= max_servers)
        {
            LOG_ERR("Server limit reached");
            return nullptr;
        }
        // throw std::runtime_error("Server limit reached.");

        auto new_server = std::make_unique<RESTServer>(
            getContext(), name); // std::unique_ptr<Server>(new RESTServer(getContext(), name));
        if (!new_server->init())
        {
            new_server.reset();
            return nullptr;
        }

        int new_id = new_server->getId();
        if (!IS_ID_VALID(new_id))
        {
            // throw std::logic_error("Server init returned invalid ID after success.");

            // удаляем сервер
            new_server.reset();
            LOG_ERR("Server got invalid id");
            return nullptr;
        }
        // Проверка на коллизию ID (маловероятно, но важно)
        if (servers.count(new_id))
        {
            // throw std::logic_error("Server ID collision detected: " + std::to_string(new_id));
            //  удаляем сервер
            new_server.reset();
            LOG_ERR("Server ID collision detected: %d", new_id);
            return nullptr;
        }

        RESTServer *raw_ptr = new_server.get();
        // Вставляем в unordered_map, перемещая владение unique_ptr
        std::lock_guard<std::mutex> lock(manager_mutex);
        servers.emplace(new_id, std::move(new_server));
        // std::cout << "EntityManager: Server '" << name << "' (ID: " << new_id << ") created." << std::endl;
        LOG_INFO("Server '%s' (ID:%d) created", name.c_str(), new_id);
        return raw_ptr;
    }

    Client *RipcEntityManager::createClient()
    {
        if (!is_initialized)
        {
            LOG_CRIT("Manager not initialized.");
            return nullptr;
        }
        // throw std::logic_error("Manager not initialized.");
        if (clients.size() >= max_clients)
        {
            LOG_ERR("Clients limit reached");
            return nullptr;
        }
        // throw std::runtime_error("Client limit reached.");

        auto new_client = std::unique_ptr<Client>(new Client(getContext()));
        if (!new_client->init())
        {
            new_client.reset();
            return nullptr;
        }

        int new_id = new_client->getId();
        if (!IS_ID_VALID(new_id))
        {
            // Это не должно произойти, если init() отработал без исключений
            // удаляем сервер
            new_client.reset();
            LOG_ERR("Client got invalid id");
            return nullptr;
            // throw std::logic_error("Client init returned invalid ID after success.");
        }
        if (clients.count(new_id))
        {
            // throw std::logic_error("Client ID collision detected: " + std::to_string(new_id));
            new_client.reset();
            LOG_ERR("Client ID collision detected: %d", new_id);
            return nullptr;
        }

        Client *raw_ptr = new_client.get();
        std::lock_guard<std::mutex> lock(manager_mutex);
        clients.emplace(new_id, std::move(new_client));
        // std::cout << "EntityManager: Client (ID: " << new_id << ") created." << std::endl;
        LOG_INFO("Client (ID: %d) created.", new_id);
        return raw_ptr;
    }

    RESTClient *RipcEntityManager::createRestfulClient()
    {
        if (!is_initialized)
        {
            LOG_CRIT("Manager not initialized.");
            return nullptr;
        }
        // throw std::logic_error("Manager not initialized.");
        if (clients.size() >= max_clients)
        {
            LOG_ERR("Clients limit reached");
            return nullptr;
        }
        // throw std::runtime_error("Client limit reached.");

        auto new_client = std::unique_ptr<RESTClient>(new RESTClient(getContext()));
        if (!new_client->init())
        {
            new_client.reset();
            return nullptr;
        }

        int new_id = new_client->getId();
        if (!IS_ID_VALID(new_id))
        {
            // Это не должно произойти, если init() отработал без исключений
            // удаляем сервер
            new_client.reset();
            LOG_ERR("Client got invalid id");
            return nullptr;
            // throw std::logic_error("Client init returned invalid ID after success.");
        }
        if (clients.count(new_id))
        {
            // throw std::logic_error("Client ID collision detected: " + std::to_string(new_id));
            new_client.reset();
            LOG_ERR("Client ID collision detected: %d", new_id);
            return nullptr;
        }

        RESTClient *raw_ptr = new_client.get();
        std::lock_guard<std::mutex> lock(manager_mutex);
        clients.emplace(new_id, std::move(new_client));
        // std::cout << "EntityManager: Client (ID: " << new_id << ") created." << std::endl;
        LOG_INFO("RESTClient (ID: %d) created.", new_id);
        return raw_ptr;
    }

    bool RipcEntityManager::deleteServer(int server_id)
    {
        if (!IS_ID_VALID(server_id))
        {
            LOG_ERR("Server got invalid id");
            return false;
        }
        std::lock_guard<std::mutex> lock(manager_mutex);

        if (!is_initialized)
        {
            LOG_CRIT("Manager not initialized");
            return false;
        }

        size_t removed_count = servers.erase(server_id);

        if (removed_count > 0)
        {
            LOG_INFO("Server ID %d deleted", server_id);
            // std::cout << "EntityManager: Server ID " << server_id << " deleted." << std::endl;
            return true;
        }
        else
        {
            LOG_ERR("Server ID %d not found", server_id);
            // std::cerr << "EntityManager: Server ID " << server_id << " not found for deletion." << std::endl;
            return false;
        }
        return false;
    }

    bool RipcEntityManager::deleteServer(Server *server)
    {
        if (!server)
        {
            LOG_ERR("Nullptr passed as server ptr");
            return false;
        }
        return deleteServer(server->m_server_id);
    }

    bool RipcEntityManager::deleteClient(int client_id)
    {
        if (!IS_ID_VALID(client_id))
        {
            LOG_ERR("Client got invalid id");
            return false;
        }

        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
        {
            LOG_CRIT("Manager not initialized");
            return false;
        }

        size_t removed_count = clients.erase(client_id);

        if (removed_count > 0)
        {
            // std::cout << "EntityManager: Client ID " << client_id << " deleted." << std::endl;
            LOG_INFO("Client ID %d deleted", client_id);
            return true;
        }
        else
        {
            // std::cerr << "EntityManager: Client ID " << client_id << " not found for deletion." << std::endl;
            LOG_ERR("Client ID %d not found", client_id);
            return false;
        }
    }

    bool RipcEntityManager::deleteClient(Client *client)
    {
        if (!client)
        {
            LOG_ERR("Nullptr passed as client ptr");
            return false;
        }
        return deleteClient(client->m_client_id);
    }

    // Поиск O(1) в среднем для unordered_map
    Server *RipcEntityManager::findServerById(int server_id)
    {
        if (!IS_ID_VALID(server_id))
        {
            LOG_ERR("Server got invalid id");
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
        {
            LOG_CRIT("Manager not initialized");
            return nullptr;
        }

        auto it = servers.find(server_id);
        return (it != servers.end()) ? it->second.get() : nullptr;
    }

    Client *RipcEntityManager::findClientById(int client_id)
    {
        if (!IS_ID_VALID(client_id))
        {
            LOG_ERR("Client got invalid id");
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!is_initialized)
        {
            LOG_CRIT("Manager not initialized");
            return nullptr;
        }

        auto it = clients.find(client_id);
        return (it != clients.end()) ? it->second.get() : nullptr;
    }

    // --- Поток и Диспетчеризация ---
    bool RipcEntityManager::registerHandler(enum notif_type type, NotificationHandler handler)
    {
        if (!IS_NTF_TYPE_VALID(type))
        {
            // throw std::invalid_argument("Invalid notification type for handler registration.");
            LOG_ERR("Invalid notification type for handler registration");
            return false;
        }
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (!handler)
        {
            LOG_WARN("Unregistering handler");
            notification_handlers.erase(type);
        }
        else
        {
            notification_handlers[type] = std::move(handler);
            // std::cout << "EntityManager: Registered custom handler for type " << type << "." << std::endl;
            LOG_INFO("Registered handler for type: %d ", type);
        }
        return true;
    }

    bool RipcEntityManager::dispatchNotification(const notification_data &ntf)
    {
        // Базовые проверки валидности
        // if (!IS_NTF_TYPE_VALID(ntf.m_type) || !IS_NTF_SEND_VALID(ntf.m_who_sends) || !IS_ID_VALID(ntf.m_reciver_id))
        if (!IS_NTF_DATA_VALID(ntf))
        {
            LOG_ERR("Dispatcher: Invalid notification received (type=%d, sender=%d, receiver=%d)", ntf.m_type,
                    ntf.m_who_sends, ntf.m_reciver_id);

            // std::cerr << "Dispatcher: Invalid notification received (type=" << ntf.m_type
            //           << ", sender=" << ntf.m_who_sends << ", receiver=" << ntf.m_reciver_id << ")" << std::endl;
            return false;
        }

        // NotificationHandler custom_handler = nullptr;
        Server *target_server = nullptr;
        Client *target_client = nullptr;
        enum notif_type current_type = static_cast<enum notif_type>(ntf.m_type);
        int receiver_id = ntf.m_reciver_id; // Копируем ID получателя

        {
            std::lock_guard<std::mutex> lock(manager_mutex);

            if (!is_initialized)
            {
                LOG_CRIT("Manager not initialized");
                return false;
            }

            // Ищем пользовательский обработчик
            auto it_handler = notification_handlers.find(current_type);
            if (it_handler != notification_handlers.end())
            {
                LOG_INFO("Found a custom handler");
                it_handler->second(ntf);
                return true;
            }

            // Если нет, ищем целевой объект (используем find под той же блокировкой)
            // if (!custom_handler)
            //{
            // К серверу
            if (ntf.m_who_sends == CLIENT)
            {
                LOG_INFO("received notification from client");
                auto it_srv = servers.find(receiver_id);
                if (it_srv != servers.end())
                {
                    LOG_INFO("Found server's handler");

                    return it_srv->second->handleNotification(ntf);
                }
            }
            // К клиенту
            else if (ntf.m_who_sends == SERVER)
            {
                LOG_INFO("received notification from server");
                auto it_cli = clients.find(receiver_id);
                if (it_cli != clients.end())
                {
                    LOG_INFO("Found client's handler");

                    return it_cli->second->handleNotification(ntf);
                }
            }
            else
                LOG_ERR("Unknown sender type: %d", ntf.m_who_sends);
            LOG_ERR("Handler was not found for type: %d", ntf.m_type);
            //}
        }

        // // Вызов обработчика/метода объекта вне блокировки
        // try
        // {
        //     if (custom_handler)
        //     {
        //         custom_handler(ntf);
        //     }
        //     else if (target_server)
        //     {
        //         target_server->handleNotification(ntf);
        //     }
        //     else if (target_client)
        //     {
        //         target_client->handleNotification(ntf);
        //     }
        //     else
        //     {
        //         // Объект не найден, и нет пользовательского обработчика
        //         std::cout << "Dispatcher: No handler or target instance found for notification type "
        //                   << ntf.m_type << " to receiver " << receiver_id << std::endl;
        //     }
        // }
        // catch (const std::exception &e)
        // {
        //     std::cerr << "Dispatcher: Exception during notification handling: " << e.what() << std::endl;
        // }
        // catch (...)
        // {
        //     std::cerr << "Dispatcher: Unknown exception during notification handling." << std::endl;
        // }
        return true;
    }

    bool RipcEntityManager::isInitialized() const
    {
        return is_initialized;
    }

    RipcEntityManager::~RipcEntityManager()
    {
        doShutdown();
    }
    bool RipcEntityManager::notificationListenerLoop()
    {
        LOG_INFO("[Listener Thread %ld]: Started", std::this_thread::get_id());
        // std::cout << "[Listener Thread " << std::this_thread::get_id() << "]: Started." << std::endl;
        int local_fd = -1;

        try
        {
            local_fd = getContext().getFd();
        }
        catch (const std::exception &e)
        {
            // std::cerr << "[Listener Thread]: Failed to get device fd: " << e.what() << ". Stopping." << std::endl;
            LOG_CRIT("[Listener Thread %d]: Failed to get device fd: %s. Stopping", std::this_thread::get_id(),
                     e.what());
            listener_running.store(false);
            return false;
        }
        if (local_fd < 0)
        {
            LOG_CRIT("[Listener Thread %d]: Failed to get device fd. Stopping", std::this_thread::get_id());
            // std::cerr << "[Listener Thread]: Failed to get device fd: " << << ". Stopping." << std::endl;
            listener_running.store(false);
            return false;
        }

        pollfd pfd;
        pfd.fd = local_fd;
        pfd.events = POLLIN; // Ждем данные для чтения
        bool ret = true;

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
                LOG_CRIT("[Listener Thread %d]: poll failed Stopping: %s", std::this_thread::get_id(), strerror(errno));
                // perror("[Listener Thread]: poll failed");
                listener_running.store(false); // Ошибка, останавливаем поток
                ret = false;
                break;
            }
            else if (ret == 0)
            {             // Таймаут
                continue; // Ничего нет
            }

            // Есть событие (ret > 0)
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                LOG_CRIT("[Listener Thread %d]: Error/Hangup/Invalid event on fd (revents: 0x%x). Stopping",
                         std::this_thread::get_id(), pfd.revents)
                // std::cerr << "[Listener Thread]: Error/Hangup/Invalid event on fd (revents: 0x"
                //           << std::hex << pfd.revents << std::dec << "). Stopping." << std::endl;
                listener_running.store(false);
                ret = false;
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
                        // Диспетчеризуем полное уведомление
                        if (!dispatchNotification(ntf))
                        {
                            LOG_WARN("Notification wasnt dispatched correctly");
                        }
                        else
                        {
                            LOG_INFO("Notification dispatched");
                        }
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
                            // Ошибка чтения, закончились данные для чтения
                            // perror("[Listener Thread]: read failed");
                            break; // Выходим из цикла чтения
                        }
                    }
                    else
                    { // bytes_read == 0 (EOF?) или прочитано меньше, чем ожидалось
                        LOG_WARN("[Listener Thread %d]: Unexpected read result %d, expected %d",
                                 std::this_thread::get_id(), bytes_read, sizeof(ntf));
                        // std::cerr << "[Listener Thread]: Unexpected read result (" << bytes_read
                        //           << ", expected " << sizeof(ntf) << "). Stopping read loop." << std::endl;
                        //  Можно попытаться прочитать остаток или просто выйти из цикла read
                        break;
                    }
                } // end read loop
            } // end if POLLIN
        } // end while(listener_running)

        // std::cout << "[Listener Thread " << std::this_thread::get_id() << "]: Exiting." << std::endl;
        LOG_INFO("[Listener Thread %d]: stopped", std::this_thread::get_id());
        return ret;
    }

} // namespace ripc