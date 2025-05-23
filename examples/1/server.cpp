#include <ripc/ripc.hpp>

int main()
{
    ripc::setLogLevel(ripc::LogLevel::WARNING);
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createRestfulServer("srv2");
    srv->add("some/<string>/<int>", [](const nlohmann::json &req) -> nlohmann::json {
        std::cout << "SERVER> got a new request: " << req.dump(4) << std::endl;
        std::cout << "SERVER> creating response\n";
        nlohmann::json js;
        js["from"] = "im server";
        js["to"] = "client";
        js["array"] = {1, 2, 3, "Asd"};
        return js;
    });

    std::cin.get();

    ripc::shutdown();
}