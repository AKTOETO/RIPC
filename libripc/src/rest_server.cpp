#include "ripc/rest_server.hpp"
#include "ripc/logger.hpp"
#include <memory>
#include <optional>

namespace ripc
{

    RESTServer::RESTServer(RipcContext &context, const std::string &str) : Server(context, str)
    {
    }
    RESTServer::~RESTServer()
    {
    }
    bool RESTServer::add(UrlPattern &&url_pattern, std::function<nlohmann::json(const nlohmann::json &request)> func)
    {
        struct ReqRes
        {
            nlohmann::json request;
            std::function<nlohmann::json(const nlohmann::json &request)> func;
            ReqRes(std::function<nlohmann::json(const nlohmann::json &request)> f) : func(f), request(nullptr) {};
        };

        auto ptr = std::make_shared<ReqRes>(func);
        return Server::registerCallback(
            std::move(url_pattern),
            [ptr](const Url &, ReadBufferView &rb) {
                LOG_INFO("setting up incoming buffer");
                auto str = rb.getPayload();
                if (str != std::nullopt && str->size() > 0 && ptr)
                {
                    LOG_INFO("got data: %*.s", str->size(), str->data());
                    ptr->request = nlohmann::json::parse(str->begin(), str->end());
                }
                else
                    LOG_INFO("There is no payload");
            },
            [ptr](WriteBufferView &wb) {
                LOG_INFO("Calling the callback with jsons");
                if (ptr && !ptr->request.empty())
                {
                    wb.setPayload(ptr->func(ptr->request).dump());
                    LOG_INFO("There is request json");
                }
                else
                {
                    wb.setPayload(ptr->func({}).dump());
                    LOG_INFO("There is no request json");
                }
                ptr->request.clear();
            });
    }
} // namespace ripc