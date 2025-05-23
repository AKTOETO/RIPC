#ifndef RIPC_REST_CLIENT_HPP
#define RIPC_REST_CLIENT_HPP
#include "client.hpp"
#include "nlohmann/json_fwd.hpp"
#include <nlohmann/json.hpp>

namespace ripc
{
    class RipcContext;
    class RipcEntityManager;

    // Клиент с RESTful интерфейсом
    class RESTClient : public Client
    {
        friend class RipcEntityManager;

      public:
        explicit RESTClient(RipcContext &context);
        ~RESTClient();
        bool get(const Url &url, std::function<void(const nlohmann::json &)>);
        bool post(const Url &url, const nlohmann::json &obj);
    };
} // namespace ripc

#endif // !RIPC_REST_CLIENT_HPP