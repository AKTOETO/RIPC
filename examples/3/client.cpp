#include "ripc/ripc.hpp"

int main()
{
    // ripc::setLogLevel(ripc::LogLevel::WARNING);
    // ripc::setCriticalLogBehavior(ripc::Logger::CriticalBehavior::LOG_ONLY);
    ripc::initialize();

    auto *cli = ripc::createClient();
    cli->connect("hello");

    cli->call(
        "alo/alo/123",
        [](ripc::ReadBufferView &rb) {
            auto data = rb.getPayload();
            if (!data)
                std::cout << "CLIENT> Server's answer: '" << rb << "'\n";
            else
                std::cout << "CLIENT> Server's answer: '" << *data << "'\n";
        },
        [](ripc::WriteBufferView &rb) {
            rb.addHeader("header1");
            rb.addHeader("123");
            rb.addHeader("gydgashds");
            rb.setPayload("Hello, im a client");
        });

    cli = ripc::createClient();
    cli->connect("hello");

    cli->call(
        "/",
        [](ripc::ReadBufferView &rb) {
            auto data = rb.getPayload();
            if (!data)
                std::cout << "CLIENT> Server's answer: '" << rb << "'\n";
            else
                std::cout << "CLIENT> Server's answer: '" << *data << "'\n";
        },
        [](ripc::WriteBufferView &rb) {
            rb.addHeader("header1");
            rb.addHeader("123");
            rb.addHeader("gydgashds");
            rb.setPayload("here is payload ) dasjdkjaskdjaslk1456789");
        });

    std::cin.get();

    ripc::shutdown();
}