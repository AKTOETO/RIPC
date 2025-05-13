#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    // создадим клиент и сервер и подключимся клиентом к серверу
    auto *srv = ripc::createServer("hello");
    srv->registerCallback(
        "alo/<string>/<int>",
        [](const ripc::Url &url, ripc::ReadBufferView &rb)
        {
            std::cout<<"SERVER> new url request: " << url << std::endl;
            auto& tokens = url.getTokens();
            std::cout<<"SERVER> string token: " << std::get<std::string>(tokens[1]) << std::endl;
            std::cout<<"SERVER> int token: " << std::get<int>(tokens[2]) << std::endl;

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