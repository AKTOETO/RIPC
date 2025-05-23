#include <ripc/ripc.hpp>

int main()
{
    ripc::setLogLevel(ripc::LogLevel::WARNING);
    ripc::initialize();

    auto cli = ripc::createRestfulClient();
    cli->connect("srv2");

    nlohmann::json j;

    // Добавляем строку
    j["stroka"] = "str";

    // Добавляем число
    j["chislo"] = 123;

    // Добавляем массив
    j["array"] = {1, 2, 3};

    // отправляем это все на сервер
    cli->post("some/url/1", j);

    std::cin.get();

    // получаем что-то с сервера
    cli->get("some/url/2", [](const nlohmann::json &json) {
        std::cout << "CLIENT> json from server : " << json.dump(4) << std::endl;
    });

    std::cin.get();

    ripc::shutdown();
}