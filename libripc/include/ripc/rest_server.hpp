#ifndef RIPC_REST_SERVER_HPP
#define RIPC_REST_SERVER_HPP
#include "server.hpp"
#include <nlohmann/json.hpp>

namespace ripc
{
    class RipcContext;
    class RipcEntityManager;

    // Клиент с RESTful интерфейсом
    class RESTServer : public Server
    {
        friend class RipcEntityManager;

      public:
        explicit RESTServer(RipcContext &context, const std::string &str);
        ~RESTServer();

        bool add(UrlPattern &&url_pattern,
                 std::function<nlohmann::json(const nlohmann::json &request)> func);
    };
} // namespace ripc

#endif // !RIPC_REST_CLIENT_HPP