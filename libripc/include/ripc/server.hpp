#ifndef RIPC_SERVER_HPP
#define RIPC_SERVER_HPP

#include "types.hpp" // Общие типы, notification_data, структуры Info/Mapping
#include "submem.hpp"
#include "url.hpp"
#include <string>
#include <vector>
#include <stdexcept> // Для исключений
#include <unordered_map>
#include <map>
#include <optional>
#include <memory>
#include <functional>

namespace ripc
{

    class RipcContext;       // Прямое объявление
    class RipcEntityManager; // Прямое объявление

    // создаем буфер нужного размера
    class Buffer : private std::string
    {
    public:
        Buffer() : std::string(SHM_REGION_PAGE_SIZE, '\0') {}
        Buffer(const char *str) : std::string(str) {};
        Buffer(const std::string &str) : std::string(str) {};
        char &operator[](size_t i) { return std::string::operator[](i); }
        size_t size() { return std::string::size(); }
        size_t length() { return std::string::length(); }
        char *data() { return std::string::data(); }
    };

    // Класс, представляющий экземпляр сервера RIPC
    class Server
    {
    private:
        friend class RipcEntityManager;

        int m_server_id = -1;
        std::string m_name;
        RipcContext &m_context;
        bool m_initialized = false;

        struct ConnectionInfo
        {
            int client_id = -1;
            const std::pair<const int, std::shared_ptr<SubMem>> &m_sub_mem_p;
            bool active = false;
            ConnectionInfo(int client_id, const std::pair<const int, std::shared_ptr<SubMem>> &sub_mem) : client_id(client_id), m_sub_mem_p(sub_mem), active(true) {}
        };

        // список соединений
        std::vector<std::shared_ptr<ConnectionInfo>> m_connections; // std::vector<ServerConnectionInfo> connections;
        // список общих памятей
        std::unordered_map<int, std::shared_ptr<SubMem>> m_mappings; // std::vector<ServerShmMapping> mappings;

        // список колбеков на url определенные
        // using Buffer = std::string;
        using UrlCallback = std::function<Buffer(const notification_data &, const Buffer &)>;
        std::map<UrlPattern, UrlCallback> m_urls;

        // Приватный конструктор
        Server(RipcContext &ctx, const std::string &server_name);

        // Приватный метод инициализации (ioctl register)
        void init();

        // Приватный метод очистки mmap
        // void cleanup_mappings();

        // Приватные хелперы для управления соединениями/маппингами
        void addConnection(int client_id, int shm_id);
        std::shared_ptr<Server::ConnectionInfo> findConnection(int client_id) const;
        const std::pair<const int, std::shared_ptr<SubMem>> &findOrCreateSHM(int shm_id);

        // Приватный метод для проверки состояния
        void checkInitialized() const;

        // Запрет копирования/присваивания
        Server(const Server &) = delete;
        Server &operator=(const Server &) = delete;

        size_t writeToClient(std::shared_ptr<ConnectionInfo> con, Buffer &&result);
        // size_t writeToClient(int client_id, size_t offset, const void *data, size_t size);
        // size_t writeToClient(int client_id, size_t offset, const std::string &text);
        // size_t readFromSubmemory(int shm_id, size_t offset, void *buffer, size_t size_to_read);
        // std::vector<char> readFromSubmemory(int shm_id, size_t offset, size_t size_to_read);

        // --- Обработка уведомлений ---
        void handleNotification(const notification_data &ntf);
        void dispatchNewMessage(const notification_data &ntf);

    public:
        // Деструктор (выполняет cleanup_mappings)
        ~Server();

        // --- Основные операции ---
        // void mmapSubmemory(int shm_id);

        // --- Получение информации ---
        int getId() const;
        const std::string &getName() const;
        bool isInitialized() const;
        std::string getInfo() const;
        bool registerCallback(UrlPattern &&url, UrlCallback &&callback);
    };

} // namespace ripc

#endif // RIPC_SERVER_HPP