#include "ripc/entity_manager.hpp" // Полное определение менеджера
#include "ripc/ripc.hpp"           // Публичный API (только объявления)

namespace ripc
{

    // Обертки над статическими методами синглтона RipcEntityManager

    void initialize(const std::string &device_path)
    {
        RipcEntityManager::getInstance().doInitialize(device_path); // Вызываем приватный метод
    }

    void shutdown()
    {
        RipcEntityManager::getInstance().doShutdown(); // Вызываем приватный метод
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

    bool deleteClient(int client_id)
    {
        return RipcEntityManager::getInstance().deleteClient(client_id);
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

    void setGlobalServerLimit(size_t limit)
    {
        RipcEntityManager::setGlobalServerLimit(limit);
    }

    void setGlobalClientLimit(size_t limit)
    {
        RipcEntityManager::setGlobalClientLimit(limit);
    }

} // namespace ripc