#include <ripc/ripc.hpp>

int main()
{
    ripc::setLogLevel(ripc::LogLevel::WARNING);
    ripc::initialize();

    auto cli = ripc::createRestfulClient();
    cli->setBlockingMode(1);
    cli->connect("srv2");

    nlohmann::json j;

    // Добавляем строку
    j["stroka"] = "str";

    // Добавляем число
    j["chislo"] = 123;

    // Добавляем массив
    j["array"] = {1, 2, 3};

    // отправляем это все на сервер
    //cli->post("some/url/1", j);

    auto call = [](const nlohmann::json &json) {
        static int i = 0;
        std::cout << "CLIENT> " << i++ << " json from server : " << json.dump() << std::endl;
        std::cout << "Client> " << json["url"].dump(4) << std::endl;
    };

    // получаем что-то с сервера
    // for (int i = 0; i < 10; i++)
    // {
    //     cli->get("some/url/" + std::to_string(i), call);
    // }
    cli->get("some/url/1", call);
    cli->get("some/url/2", call);
    cli->get("some/url/3", call);
    cli->get("some/url/4", call);

    ripc::shutdown();
}