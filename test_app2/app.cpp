#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createServer("server");
    srv->registerCallback(
        "alo/alo/<int>",
        [](const notification_data &ntf, const ripc::Buffer &input, ripc::Buffer &out)
        {
            (void)ntf;
            std::cout << "server> input[" << input << "]\n";
            out.copy_from(std::string("stroka"));
        });



    auto *cli = ripc::createClient();
    cli->connect("server");

    ripc::Buffer buf;
    buf.copy_from("kak dela???", 12);

    cli->call(
        "alo/alo/12", std::move(buf),
        [](ripc::Buffer buffer)
        {
            std::cout << buffer << std::endl;
        });

    std::cin.get();

    ripc::shutdown();
}