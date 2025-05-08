#ifndef RIPC_CLIENT_HPP
#define RIPC_CLIENT_HPP

#include "types.hpp" // Общие типы, notification_data
#include "submem.hpp"
#include "url.hpp"

#include <functional>
#include <string>
#include <vector>
#include <stdexcept> // Для исключений

namespace ripc
{
    class RipcContext;
    class RipcEntityManager;

    // Класс, представляющий экземпляр клиента RIPC
    class Client
    {
    private:
        friend class RipcEntityManager; // Фабрика имеет доступ к конструктору и init
        using CallCallback = std::function<void(Buffer buffer)>;

        int m_client_id = -1;
        RipcContext &m_context;              // Ссылка на общий контекст
        bool m_initialized = false;          // Успешно ли прошел init (вызов ioctl)
        std::string m_connected_server_name; // имя сервера, к котрому подключен
        CallCallback m_callback;             // Обработчик ответа от сервера
        bool m_is_request_sent;              // отправлен ли запрос

        // Информация о разделяемой памяти
        // void *shm_addr = MAP_FAILED;
        // size_t shm_size = 0;
        // bool shm_mapped = false;
        Memory m_sub_mem;

        // Приватный конструктор (вызывается только RipcEntityManager)
        explicit Client(RipcContext &ctx);

        // Приватный метод инициализации (выполняет ioctl register)
        void init();

        // Приватный метод для очистки mmap
        // void cleanup_shm();

        // Приватный метод для проверки состояния
        void checkInitialized() const;
        void checkMapped() const;

        // Запрет копирования/присваивания
        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;

        // чтение или запись
        //size_t write(size_t offset, const void *data, size_t size);
        //size_t write(size_t offset, const std::string &text);
        //size_t read(size_t offset, void *buffer, size_t size_to_read);
        //std::vector<char> read(size_t offset, size_t size_to_read);

        // --- Обработка уведомлений ---
        void handleNotification(const notification_data &ntf);
        void dispatchNewMessage(const notification_data &ntf);

    public:
        ~Client();

        /// @brief Подключение к серверу
        /// @param server_name имя сервера
        void connect(const std::string &server_name);

        /// @brief отключение от сервера
        void disconnect();

        // void mmap();

        /// @brief Отправка запроса на сервер
        /// @param url URL, на котоырй уходит запрос в сервер
        /// @param callback обработчик ответа сервера
        /// @return был ли отправлен запрос
        bool call(const Url &url, CallCallback callback = nullptr);

        /// @brief Отправка запроса на сервер
        /// @param url URL, на котоырй уходит запрос в сервер
        /// @param buffer Данные, отправляемые на сервер вместе с запросом
        /// @param callback обработчик ответа сервера
        /// @return был ли отправлен запрос
        bool call(const Url &url, Buffer &&buffer, CallCallback callback = nullptr);

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
    };

} // namespace ripc

#endif // RIPC_CLIENT_HPP