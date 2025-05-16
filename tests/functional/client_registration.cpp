#include "ripc/ripc.hpp"
#include <gtest/gtest.h>

// регистрация клиента
TEST(ClientRegistartion, UniqueName)
{
    ASSERT_NE(ripc::createClient(), nullptr) << "Could not connect to driver";
}

// регистрация нескольких клиентов
TEST(ClientRegistartion, NotUniqueName)
{
    EXPECT_NE(ripc::createClient(), nullptr);
    EXPECT_NE(ripc::createClient(), nullptr);
}

// попытка регистрации очень большого числа серверов
TEST(ClientRegistartion, TooMuchClient)
{
    ripc::shutdown();
    ripc::initialize();

    // занимаем все возмодные места под сервера
    for (int i = 0; i < ripc::DEFAULTS::MAX_CLIENTS; i++)
    {
        EXPECT_NE(ripc::createClient(), nullptr);
    }

    // пытаемся зарегистрировать еще один
    ASSERT_EQ(ripc::createClient(), nullptr) << "Couldnt create client more then " 
                                             << ripc::DEFAULTS::MAX_CLIENTS;
}

// создание удаление клиента
TEST(ClientRegistartion, CreataionAfterDeletion)
{
    ripc::shutdown();
    ripc::initialize();

    auto client = ripc::createClient();

    ASSERT_NE(client, nullptr);
    ASSERT_EQ(ripc::deleteClient(client), true);
    ASSERT_NE(ripc::createClient(), nullptr);
}

int main(int argc, char **argv)
{
    // чтобы логов не было из библиотеки
    ripc::setLogLevel(ripc::LogLevel::NONE);
    ripc::initialize();
    // чтобы исключений не было из библиотеки
    ripc::setCriticalLogBehavior(ripc::Logger::CriticalBehavior::LOG_ONLY);

    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();

    ripc::shutdown();
    return ret;
}