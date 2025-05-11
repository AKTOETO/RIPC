#include "ripc/entity_manager.hpp" // Полное определение менеджера
#include "ripc/ripc.hpp"           // Публичный API (только объявления)

namespace ripc
{

    // Обертки над статическими методами синглтона RipcEntityManager

    void initialize(const std::string &device_path)
    {
        RipcEntityManager::getInstance().doInitialize(device_path);
    }

    void shutdown()
    {
        RipcEntityManager::getInstance().doShutdown();
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

    void registerNotificationHandler(enum notif_type type, NotificationHandler handler)
    {
        RipcEntityManager::getInstance().registerHandler(type, std::move(handler));
    }
} // namespace ripc