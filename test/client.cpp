#include "client.h"

#include <thread>

void read_input(std::shared_ptr<IPC::Client> client) {
    std::string message;
    while (std::getline(std::cin, message) && message!="exit") {
        if (!message.empty()) {
            client->sendMessage(message);
        }
    }
}

int main()
{
	try {
		boost::asio::io_service service;

		// Создание клиента и подключение к серверу
		auto cl = std::make_shared<IPC::Client>(service, "/tmp/ripc_test1.com");
		
		std::thread th(read_input,cl);

		boost::asio::io_service::work w(service);
		service.run();

		th.join();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
	}
	return 0;
}