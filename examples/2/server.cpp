#include "ripc/ripc.hpp"

int main()
{
    ripc::setLogLevel(ripc::LogLevel::WARNING);
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createServer("srv2");
    srv->registerCallback(
        "some/<string>/<int>",
        [](const ripc::Url &url, ripc::ReadBufferView &rb) {
            std::cout << "url: " << url.getUrl() << std::endl;
            // печать всех загловков
            std::optional<std::string_view> header;
            while ((header = rb.getHeader()))
            {
                std::cout << "header: " << *header << std::endl;
            }
            // печать полезной нагрузки
            auto payload = rb.getPayload();
            if (payload)
                std::cout << "SERVER> payload from client: " << *payload << std::endl;
        },
        [](ripc::WriteBufferView &wb) {
            nlohmann::json js;
            js["from"] = "im server";
            js["to"] = "client";
            js["array"] = {1, 2, 3, "Asd"};

            wb.setPayload(js.dump());
        });

    std::cin.get();

    ripc::shutdown();
}