#include "ripc/entity_manager.hpp"
#include "ripc/ripc.hpp"

namespace ripc
{

    // Обертки над статическими методами синглтона RipcEntityManager

    bool initialize(const std::string &device_path)
    {
        return RipcEntityManager::getInstance().doInitialize(device_path);
    }

    bool shutdown()
    {
        return RipcEntityManager::getInstance().doShutdown();
    }

    Server *createServer(const std::string &name)
    {
        return RipcEntityManager::getInstance().createServer(name);
    }

    Client *createClient()
    {
        return RipcEntityManager::getInstance().createClient();
    }

    bool deleteServer(int server_id)
    {
        return RipcEntityManager::getInstance().deleteServer(server_id);
    }

    bool deleteServer(Server *server)
    {
        return RipcEntityManager::getInstance().deleteServer(server);
    }

    bool deleteClient(int client_id)
    {
        return RipcEntityManager::getInstance().deleteClient(client_id);
    }

    bool deleteClient(Client *client)
    {
        return RipcEntityManager::getInstance().deleteClient(client);
    }
    Server *findServerById(int server_id)
    {
        return RipcEntityManager::getInstance().findServerById(server_id);
    }

    Client *findClientById(int client_id)
    {
        return RipcEntityManager::getInstance().findClientById(client_id);
    }

    bool registerNotificationHandler(enum notif_type type, NotificationHandler handler)
    {
        return RipcEntityManager::getInstance().registerHandler(type, std::move(handler));
    }

    // --- Реализация API для управления логгированием ---
    void setLogLevel(LogLevel level)
    {
        Logger::getInstance().setLevel(level);
    }

    void setLogStream(std::ostream *os)
    {
        Logger::getInstance().setOutputStream(os);
    }

    void setCriticalLogBehavior(Logger::CriticalBehavior behavior)
    {
        Logger::getInstance().setCriticalBehavior(behavior);
    }

} // namespace ripc