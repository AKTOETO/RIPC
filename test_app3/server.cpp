#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createServer("hello");
    srv->registerCallback(
        "alo/da",
        [](const notification_data &ntf, const ripc::Buffer &input, ripc::Buffer &out)
        {
            (void)ntf;
            std::cout << "server> input[" << input << "]\n";
            out.copy_from(std::string("stroka"));
        });

    std::cin.get();

    ripc::shutdown();
}