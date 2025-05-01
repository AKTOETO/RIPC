#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *cli = ripc::createClient();
    auto *srv = ripc::createServer("server");

    cli->connect("server");
    cli->mmap();
    cli->write(0, "text");    
    

    ripc::shutdown();
}