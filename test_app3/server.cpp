#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createServer("hello");
    srv->registerCallback(
        "alo/da",
        [](const ripc::Url &url, ripc::ReadBufferView &rb)
        {
            auto header = rb.getHeader();
            if (!header)
                std::cout << "SERVER> there is no additional headers\n";
            else
                std::cout << "SERVER> aditional header: '" << *header << "'\n";
                
            auto data = rb.getPayload();
            if (!data)
                std::cout
                    << "SERVER> got a new request using "
                    << url << " data: '" << rb
                    << "'\n";
            else
                std::cout
                    << "SERVER> got a new request using "
                    << url << " data: '" << *data
                    << "'\n";
        },
        [](ripc::WriteBufferView &wb)
        {
            wb.setPayload("Hello,client. Im a server");
        });
    std::cin.get();

    ripc::shutdown();
}