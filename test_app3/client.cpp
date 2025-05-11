#include "ripc/ripc.hpp"

int main()
{
    ripc::initialize();

    auto *cli = ripc::createClient();
    cli->connect("hello");

    cli->call(
        "alo/da",
        [](ripc::ReadBufferView &rb)
        {
            auto data = rb.getPayload();
            if (!data)
                std::cout << "CLIENT> Server's answer: '" << rb << "'\n";
            else
                std::cout << "CLIENT> Server's answer: '" << *data << "'\n";
        },
        [](ripc::WriteBufferView &rb)
        {
            rb.addHeader("header1");
            rb.addHeader("123");
            rb.addHeader("gydgashds");
            rb.setPayload("Hello, im a client");
        });

    std::cin.get();

    ripc::shutdown();
}