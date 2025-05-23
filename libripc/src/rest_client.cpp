#include "ripc/rest_client.hpp"
#include "ripc/logger.hpp"
#include "ripc/submem.hpp"

namespace ripc
{
    RESTClient::RESTClient(RipcContext &context)
    :Client(context)
    {

    }
    RESTClient::~RESTClient()
    {
    }
    bool RESTClient::get(const Url &url, std::function<void(const nlohmann::json &)> out)
    {
        return Client::call(
            url,
            [out](ripc::ReadBufferView &rb) {
                auto data = rb.getPayload();
                if (!data)
                {
                    LOG_ERR("Where is no payload");
                    return;
                }
                out(nlohmann::json::parse(*data));
            },
            nullptr);
    };
    bool RESTClient::post(const Url &url, const nlohmann::json &json)
    {
        return Client::call(url, nullptr, 
            [&json](ripc::WriteBufferView &wb) 
            {
                 wb.setPayload(json.dump()); 
            }
        );
    }
} // namespace ripc