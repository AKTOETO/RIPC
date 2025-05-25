#ifndef RIPC_CLIENT_HPP
#define RIPC_CLIENT_HPP

#include "submem.hpp"
#include "types.hpp" // Общие типы, notification_data
#include "url.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept> // Для исключений
#include <string>
#include <vector>

namespace ripc
{
    class RipcContext;
    class RipcEntityManager;

    // Класс, представляющий экземпляр клиента RIPC
    class Client
    {
      private:
        friend class RipcEntityManager;

        // Обработчик запросов
        using CallbackIn = std::function<void(ReadBufferView &)>;
        using CallbackOut = std::function<void(WriteBufferView &)>;
        struct CallbackFull
        {
            CallbackIn m_in;
            CallbackOut m_out;
        };

        int m_client_id = -1;
        RipcContext &m_context;              // Ссылка на общий контекст
        bool m_initialized = false;          // Успешно ли прошел init (вызов ioctl)
        std::string m_connected_server_name; // имя сервера, к котрому подключен
        CallbackIn m_callback;               // Обработчик ответа от сервера
        bool m_is_request_sent;              // отправлен ли запрос
        bool m_is_using_blocking;            // используется ли блокирующий режим
        bool m_is_running;                   // работает ли еще
        bool m_is_frozen;                    // заморожен ли поток
        std::mutex m_lock;                   // блокировка доступа
        std::condition_variable m_cv;        // блокиовка потока

        // Информация о разделяемой памяти
        Memory m_sub_mem;

        // Приватный метод инициализации (выполняет ioctl register)
        bool init();

        // Приватный метод для проверки состояния
        // bool checkInitialized() const;
        // bool checkMapped() const;

        // Запрет копирования/присваивания
        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;

        // --- Обработка уведомлений ---
        bool handleNotification(const notification_data &ntf);
        bool dispatchNewMessage(const notification_data &ntf);
        bool dispatchRemoteDisconnect(const notification_data &ntf);

      public:
        explicit Client(RipcContext &ctx);
        ~Client();

        /// @brief Подключение к серверу
        /// @param server_name имя сервера
        bool connect(const std::string &server_name);

        /// @brief отключение от сервера
        bool disconnect();

        /// @brief Отправка запроса на сервер
        /// @param url URL запроса
        /// @param in обработчик ответа от сервера
        /// @param out обработчик отправляемых данных
        /// @return отправлены данные (1) или нет (0)
        bool call(const Url &url, CallbackIn &&in, CallbackOut &&out);

        // --- Получение информации ---
        // получение id
        int getId() const;
        // Проверяет успешность init()
        bool isInitialized() const;
        // есть ли подключение к серверу
        bool isConnected() const;
        // отображена ли память
        bool isMapped() const;
        // Форматированная строка для вывода
        std::string getInfo() const;

        /**
         * @brief Установка состояния работы класса
         *
         * @param mode 1 - блокриующий режим. 0 - ассинхронный режим
         */
        void setBlockingMode(bool mode);
    };

} // namespace ripc

#endif // RIPC_CLIENT_HPP