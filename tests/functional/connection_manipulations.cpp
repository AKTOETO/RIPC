#include "../tests.hpp"

class ConnManip : public RipcTest{};

// Попытка подключения клиента к несуществующему серверу
TEST_F(ConnManip, NotSucClientConnToServer)
{
    auto cl = ripc::createClient();
    ASSERT_NE(cl, nullptr);
    ASSERT_EQ(cl->connect("NotSucClientConnToServer"), false);
}

// Подключение клиента к существующему серверу
TEST_F(ConnManip, SucClientConnToServer)
{
    auto cl = ripc::createClient();
    auto sr = ripc::createServer("SucClientConnToServer");
    ASSERT_NE(cl, nullptr);
    ASSERT_NE(sr, nullptr);
    ASSERT_EQ(cl->connect("SucClientConnToServer"), 1);
}

// Подключение клиентов к существующему серверу
TEST_F(ConnManip, MultipleClientsConnect)
{
    auto cl1 = ripc::createClient();
    auto cl2 = ripc::createClient();
    auto cl3 = ripc::createClient();
    auto sr = ripc::createServer("MultipleClientsConnect");
    ASSERT_NE(cl1, nullptr);
    ASSERT_NE(cl2, nullptr);
    ASSERT_NE(cl3, nullptr);
    ASSERT_NE(sr, nullptr);
    ASSERT_EQ(cl1->connect("MultipleClientsConnect"), 1) << "First client";
    ASSERT_EQ(cl2->connect("MultipleClientsConnect"), 1) << "Second client";
    ASSERT_EQ(cl3->connect("MultipleClientsConnect"), 1) << "Third client";
}

// Подключение клиента к разным существующим серверам
TEST_F(ConnManip, MultipleServerConnection)
{    
    auto cl = ripc::createClient();
    auto sr1 = ripc::createServer("1:MultipleServerConnection");
    auto sr2 = ripc::createServer("2:MultipleServerConnection");
    ASSERT_NE(cl, nullptr);
    ASSERT_NE(sr1, nullptr);
    ASSERT_NE(sr2, nullptr);

    ASSERT_EQ(cl->connect("1:MultipleServerConnection"), 1) << "First connection";
    ASSERT_EQ(cl->disconnect(), 1) << "Disconnection";
    ASSERT_EQ(cl->connect("2:MultipleServerConnection"), 1) << "Second connection";
}

// Корректный разрыв соединения со стороны клиента
TEST_F(ConnManip, ClientDisconnect)
{    
    auto cl = ripc::createClient();
    auto sr = ripc::createServer("ClientDisconnect");
    ASSERT_NE(cl, nullptr);
    ASSERT_NE(sr, nullptr);

    ASSERT_EQ(cl->connect("ClientDisconnect"), 1);
    ASSERT_EQ(cl->disconnect(), 1);
}

// Корректный разрыв соединения со стороны сервера
TEST_F(ConnManip, ServerDisconnect)
{    
    auto cl = ripc::createClient();
    auto sr = ripc::createServer("ClientDisconnect");
    ASSERT_NE(cl, nullptr);
    ASSERT_NE(sr, nullptr);

    ASSERT_EQ(cl->connect("ClientDisconnect"), 1);
    sleep(2);
    ASSERT_EQ(sr->disconnect(cl->getId()), 1);
}

int main(int argc, char **argv)
{
    // чтобы логов не было из библиотеки
    ripc::setLogLevel(ripc::LogLevel::NONE);
    
    // чтобы исключений не было из библиотеки
    ripc::setCriticalLogBehavior(ripc::Logger::CriticalBehavior::LOG_ONLY);

    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();

    //ripc::shutdown();
    return ret;
}