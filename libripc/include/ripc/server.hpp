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

    // Класс, представляющий экземпляр сервера RIPC
    class Server
    {
    private:
        friend class RipcEntityManager;

        // Обработчик запросов на определенный url
        using UrlCallbackIn = std::function<void(const Url&, ReadBufferView &)>;
        using UrlCallbackOut = std::function<void(WriteBufferView &)>;
        struct UrlCallbackFull
        {
            UrlCallbackIn m_in;            
            UrlCallbackOut m_out;
        };

        int m_server_id;
        std::string m_name;
        RipcContext &m_context;
        bool m_initialized;

        struct ConnectionInfo
        {
            int client_id = -1;
            const std::pair<const int, std::shared_ptr<Memory>> &m_sub_mem_p;
            bool active = false;
            ConnectionInfo(int client_id, const std::pair<const int, std::shared_ptr<Memory>> &sub_mem)
                : client_id(client_id), m_sub_mem_p(sub_mem), active(true)
            {
            }
        };

        // список соединений
        std::vector<std::shared_ptr<ConnectionInfo>> m_connections; // std::vector<ServerConnectionInfo> connections;
        // список общих памятей
        std::unordered_map<int, std::shared_ptr<Memory>> m_mappings; // std::vector<ServerShmMapping> mappings;

        // список колбеков на url определенные
        std::map<UrlPattern, UrlCallbackFull> m_urls;

        // Приватный конструктор
        Server(RipcContext &ctx, const std::string &server_name);

        // Приватный метод инициализации (ioctl register)
        void init();

        // Приватные хелперы для управления соединениями/маппингами
        void addConnection(int client_id, int shm_id);
        std::shared_ptr<Server::ConnectionInfo> findConnection(int client_id) const;
        const std::pair<const int, std::shared_ptr<Memory>> &findOrCreateSHM(int shm_id);

        // Приватный метод для проверки состояния
        void checkInitialized() const;

        // Запрет копирования/присваивания
        Server(const Server &) = delete;
        Server &operator=(const Server &) = delete;

        void writeToClient(std::shared_ptr<ConnectionInfo> con, WriteBufferView &result);

        // --- Обработка уведомлений ---
        void handleNotification(const notification_data &ntf);
        void dispatchNewMessage(const notification_data &ntf);

        // отключение клиента
        void disconnectFromClient(std::shared_ptr<ConnectionInfo> con);

    public:
        ~Server();

        // --- Получение информации ---
        int getId() const;
        const std::string &getName() const;
        bool isInitialized() const;
        std::string getInfo() const;

        // регистрация обработчика запросов на шаблонный url
        bool registerCallback(UrlPattern &&url_pattern, UrlCallbackFull &&callback);
        bool registerCallback(UrlPattern &&url_pattern, UrlCallbackIn &&in, UrlCallbackOut&& out);
    };

} // namespace ripc

#endif // RIPC_SERVER_HPP