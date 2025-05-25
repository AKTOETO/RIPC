#include "../tests.hpp"
#include "ripc/ripc.hpp"
#include "ripc/submem.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

class ManyRequests : public RipcTest
{
  protected:
    void runTest(int requestCount, int payloadSize)
    {
        const std::string &testName{"requestCount" + std::to_string(requestCount) + "payloadSize" +
                                    std::to_string(payloadSize)};
        auto srv = ripc::createRestfulServer(testName);
        ASSERT_NE(srv, nullptr);

        srv->add("some/url/<int>", [&](const nlohmann::json &req) -> nlohmann::json {
            return {{"status", "hello from server"}, {"payload", req}};
        });

        std::vector<long> results;
        results.reserve(requestCount + 1);

        std::thread t([&]() {
            auto cli = ripc::createRestfulClient();
            ASSERT_NE(cli, nullptr);
            cli->setBlockingMode(1);
            ASSERT_EQ(cli->connect(testName), 1);

            auto full_start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < requestCount; ++i)
            {
                nlohmann::json js{std::string(payloadSize, 'n')};
                
                auto start = std::chrono::high_resolution_clock::now();
                ASSERT_EQ(cli->post("some/url/" + std::to_string(i), js), 1);
                auto end = std::chrono::high_resolution_clock::now();

                results.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
            }
            auto full_end = std::chrono::high_resolution_clock::now();
            results.push_back(std::chrono::duration_cast<std::chrono::microseconds>(full_end - full_start).count());
        });
        t.join();

        std::ofstream fout(testName + ".log");
        for (int i = 0; i < requestCount; ++i)
            fout << "(" << i << "," << results[i] << "),";
        fout << "\n" << results.back();
    }
};

TEST_F(ManyRequests, 100)
{
    runTest(50, 100);
}

TEST_F(ManyRequests, 200)
{
    runTest(50, 150);
}

TEST_F(ManyRequests, 300)
{
    runTest(50, 300);
}

TEST_F(ManyRequests, 400)
{
    runTest(50, 400);
}

TEST_F(ManyRequests, 500)
{
    runTest(50, 500);
}

TEST_F(ManyRequests, 1000)
{
    runTest(50, 1000);
}

TEST_F(ManyRequests, 2000)
{
    runTest(50, 2000);
}

TEST_F(ManyRequests, 2500)
{
    runTest(50, 2500);
}

int main(int argc, char **argv)
{
    // ripc::setLogLevel(ripc::LogLevel::INFO);
    std::ofstream fout("many_requests.log");
    ripc::setLogStream(&fout);
    ripc::initialize();

    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();

    ripc::shutdown();
    return ret;
}
