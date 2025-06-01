#include "../tests.hpp"
#include "ripc/ripc.hpp"
#include "ripc/submem.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

class ManyClients : public RipcTest
{
  protected:
    void runTest(int clientsCount, int payloadSize)
    {
        const std::string &testName{"clientsCount" + std::to_string(clientsCount) + "payloadSize" +
                                    std::to_string(payloadSize)};
        auto srv = ripc::createRestfulServer(testName);
        ASSERT_NE(srv, nullptr);

        srv->add("some/url/<int>", [&](const nlohmann::json &req) -> nlohmann::json {
            return {{"status", "hello from server"}, {"payload", req}};
        });

        std::vector<std::thread> clients;
        clients.reserve(clientsCount);

        for (auto i = 0; i < clientsCount; i++)
            clients.emplace_back([&]() {
                auto cli = ripc::createRestfulClient();
                ASSERT_NE(cli, nullptr);
                cli->setBlockingMode(1);
                ASSERT_EQ(cli->connect(testName), 1);

                auto full_start = std::chrono::high_resolution_clock::now();
                for (int i = 0; i < clientsCount; ++i)
                {
                    nlohmann::json js{std::string(payloadSize, 'n')};

                    auto start = std::chrono::high_resolution_clock::now();
                    ASSERT_EQ(cli->post("some/url/" + std::to_string(i), js), 1);
                    auto end = std::chrono::high_resolution_clock::now();
                }
                auto full_end = std::chrono::high_resolution_clock::now();
            });

        for (auto &el : clients)
            el.join();
    }
};

TEST_F(ManyClients, 5)
{
    runTest(5, 4000);
}

TEST_F(ManyClients, 10)
{
    runTest(10, 4000);
}
TEST_F(ManyClients, 15)
{
    runTest(5, 4000);
}

int main(int argc, char **argv)
{
    // ripc::setLogLevel(ripc::LogLevel::INFO);
    ripc::initialize();

    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();

    ripc::shutdown();
    return ret;
}
