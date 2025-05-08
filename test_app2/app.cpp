#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createServer("server");
    srv->registerCallback(
        "alo/alo",
        [](const notification_data &ntf, const ripc::Buffer &input, ripc::Buffer &out)
        {
            (void)ntf;
            std::cout << "server> input[" << input << "]\n";
            out.copy_from(std::string("stroka"));
        });



    auto *cli = ripc::createClient();
    cli->connect("server");

    ripc::Buffer buf;
    auto cpid = buf.copy_from("kak dela???", 11);

    cli->call(
        "alo/alo1", std::move(buf),
        [](ripc::Buffer buffer)
        {
            std::cout << buffer << std::endl;
        });

    std::cin.get();

    ripc::shutdown();
}