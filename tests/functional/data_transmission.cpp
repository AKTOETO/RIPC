#include <chrono>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include "../tests.hpp"

class DataTransm : public RipcTest{};

TEST_F(DataTransm, ClientToServer)
{
    auto cl = ripc::createClient();
    auto srv = ripc::createServer("ClientToServer");

    ASSERT_NE(cl, nullptr) << "Client creation failed";
    ASSERT_NE(srv, nullptr) << "Server creation failed";

    const std::string send_data = "test_data_123";
    std::promise<bool> callback_promise;
    auto callback_future = callback_promise.get_future();

    // Регистрация callback на сервере
    auto reg_res = srv->registerCallback(
        "/test/endpoint",
        [&callback_promise, send_data](const ripc::Url &url, ripc::ReadBufferView &rb) {
            auto data = rb.getPayload();
            if (data)
            {
                if (*data == send_data)
                {
                    callback_promise.set_value(true);
                }
                else
                {
                    callback_promise.set_value(false);
                }
            }
        },
        nullptr);
    ASSERT_TRUE(reg_res) << "Callback registration failed";

    // Подключение клиента
    ASSERT_TRUE(cl->connect("ClientToServer")) << "Connection failed";

    // Асинхронный вызов с синхронизацией
    std::promise<bool> call_promise;
    auto call_future = call_promise.get_future();

    auto call_res = cl->call("/test/endpoint", nullptr,
                             [&](ripc::WriteBufferView &wb) { call_promise.set_value(wb.setPayload(send_data)); });

    // Ожидаем завершения операции записи
    ASSERT_NE(call_future.wait_for(std::chrono::seconds(4)), std::future_status::timeout)
        << "Write operation timed out";
    ASSERT_TRUE(call_future.get()) << "Payload write failed";

    // Ожидаем callback на сервере
    ASSERT_NE(callback_future.wait_for(std::chrono::seconds(4)), std::future_status::timeout) << "Callback timed out";
    ASSERT_TRUE(callback_future.get()) << "Data validation failed";
}

TEST_F(DataTransm, ServerToClient) {
    auto cl = ripc::createClient();
    auto srv = ripc::createServer("ServerToClient");

    ASSERT_NE(cl, nullptr);
    ASSERT_NE(srv, nullptr);

    const std::string send_data = "server_data_456";
    std::promise<bool> callback_promise;
    auto callback_future = callback_promise.get_future();

    // Регистрация callback на сервере для отправки данных
    auto reg_res = srv->registerCallback(
        "/test/endpoint",
        nullptr,
        [&](ripc::WriteBufferView &wb) {
            callback_promise.set_value(wb.setPayload(send_data));
        }
    );
    ASSERT_TRUE(reg_res) << "Server callback registration failed";

    // Подключение клиента
    ASSERT_TRUE(cl->connect("ServerToClient")) << "Connection failed";

    // Асинхронный вызов с проверкой данных
    std::promise<bool> call_promise;
    auto call_future = call_promise.get_future();

    auto call_res = cl->call(
        "/test/endpoint",
        [&](ripc::ReadBufferView &rb) {
            auto data = rb.getPayload();
            call_promise.set_value(data && (*data == send_data));
        },
        nullptr
    );

    // Проверка результата
    ASSERT_NE(call_future.wait_for(std::chrono::seconds(2)), std::future_status::timeout)
        << "Read operation timed out";
    ASSERT_TRUE(call_future.get()) << "Data validation failed";
}

int main(int argc, char **argv)
{
    ripc::setLogLevel(ripc::LogLevel::WARNING);
    ripc::setCriticalLogBehavior(ripc::Logger::CriticalBehavior::LOG_ONLY);

    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();

    ripc::shutdown();
    return ret;
}