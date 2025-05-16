#include "ripc/ripc.hpp"
#include <gtest/gtest.h>

/**
 * === SERVERS ===
 */

// регистрация сервера с новым именем
TEST(ServerRegistartion, UniqueName)
{
    ASSERT_NE(ripc::createServer("UniqueName"), nullptr) << "Could not connect to driver";
}

// регистрация сервера с существующим именем
TEST(ServerRegistartion, NotUniqueName)
{
    EXPECT_NE(ripc::createServer("NotUniqueName"), nullptr);
    EXPECT_EQ(ripc::createServer("NotUniqueName"), nullptr);
}

// попытка зарегистрировать сервер с большой длинной имени
TEST(ServerRegistartion, TooLongName)
{
    ASSERT_EQ(ripc::createServer(std::string(70, 'A')), nullptr);
}

// попытка регистрации очень большого числа серверов
TEST(ServerRegistartion, TooMuchServers)
{
    ripc::shutdown();
    ripc::initialize();

    // занимаем все возмодные места под сервера
    for (int i = 0; i < ripc::DEFAULTS::MAX_SERVERS; i++)
    {
        EXPECT_NE(ripc::createServer("TooMuchServers: " + std::to_string(i)), nullptr);
    }

    // пытаемся зарегистрировать еще один
    ASSERT_EQ(ripc::createServer("TooMuchServers: One more"), nullptr);
}

// создание удаление сервера и попытка создать еще один с его именем
TEST(ServerRegistartion, CreataionAfterDeletion)
{
    ripc::shutdown();
    ripc::initialize();

    auto server = ripc::createServer("CreataionAfterDeletion");

    ASSERT_NE(server, nullptr);
    ASSERT_EQ(ripc::deleteServer(server), true);
    ASSERT_NE(ripc::createServer("CreataionAfterDeletion"), nullptr);
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