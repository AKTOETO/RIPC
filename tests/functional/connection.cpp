#include "../tests.hpp"


// тестирование возможность подключения к драйверу
TEST(Connection, SuccessfullConnection)
{
    ASSERT_EQ(ripc::initialize(), 1) << "Could not connect to driver";
}

// тестирование отключения от драйвера
TEST(Connection, SuccessfulDisconnection)
{
    ASSERT_EQ(ripc::shutdown(), 1) << "Could not disconnect from driver";
}

// Попытка использовать функции драйвера до подключения к нему
TEST(Connection, UseBeforConnection)
{
    EXPECT_EQ(ripc::createClient(), nullptr);
    EXPECT_EQ(ripc::createServer(""), nullptr);
}

// Попытка использовать функции драйвера после подключения к нему подключения к нему
TEST(Connection, UseAfterConnection)
{
    ASSERT_EQ(ripc::initialize(), 1) << "Could not connect to driver";
    EXPECT_NE(ripc::createClient(), nullptr);
    EXPECT_NE(ripc::createServer("UseBeforConnection"), nullptr);
    ASSERT_EQ(ripc::shutdown(), 1) << "Could not disconnect from driver";
}

int main(int argc, char **argv)
{
    // чтобы логов не было из библиотеки
    ripc::setLogLevel(ripc::LogLevel::NONE);
    // чтобы исключений не было из библиотеки
    ripc::setCriticalLogBehavior(ripc::Logger::CriticalBehavior::LOG_ONLY);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}