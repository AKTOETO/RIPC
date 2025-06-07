#include <ripc/ripc.hpp>

int main()
{
    // ripc::setLogLevel(ripc::LogLevel::WARNING);
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createRestfulServer("srv2");
    srv->add("some/<string>/<int>", [](const nlohmann::json &req) -> nlohmann::json {
        std::cout << "SERVER> got a new request: " << req.dump(4) << std::endl;
        nlohmann::json js = req;
        js["status"] = 1;
        std::cout << "SERVER> created response: " << js.dump() << std::endl;
        return js;
    });

    std::cin.get();

    ripc::shutdown();
}