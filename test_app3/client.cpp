#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    auto *cli = ripc::createClient();
    cli->connect("hello");

    ripc::Buffer buf;
    buf.copy_from("kak dela???", 12);

    cli->call(
        "alo/da", std::move(buf),
        [](ripc::Buffer buffer)
        {
            std::cout << buffer << std::endl;
        });

    std::cin.get();

    ripc::shutdown();
}