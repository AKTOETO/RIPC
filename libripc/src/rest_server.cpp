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
            [ptr](const Url &url, ReadBufferView &rb) {
                LOG_INFO("Calling the input callback for REST");
                // записываем url
                LOG_INFO("From url: ", url.getUrl().c_str());
                auto &json = ptr->request;
                json["url"] = nlohmann::json::array();
                for (const auto &token : url.getTokens())
                {
                    std::visit([&](const auto &val) { json["url"].push_back(val); }, token);
                }

                // получаем полезную нагрузку
                auto str = rb.getPayload();

                // записываем полезную нагрузку, если она есть
                if (str != std::nullopt && str->size() > 0 && ptr)
                {
                    LOG_INFO("got data: %*.s", str->size(), str->data());
                    ptr->request["payload"] = nlohmann::json::parse(str->begin(), str->end());
                }
                else
                    LOG_INFO("There is no payload");
            },
            [ptr](WriteBufferView &wb) {
                LOG_INFO("Calling the output callback for REST");
                wb.setPayload(ptr->func(ptr->request).dump());
                ptr->request.clear();
            });
    }
} // namespace ripc