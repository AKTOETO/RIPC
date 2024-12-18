#include "client.h"

#include <thread>

using namespace std::chrono_literals;

void read_input(std::shared_ptr<IPC::Client> client) {
	std::string message;
	while (std::getline(std::cin, message) && message != "exit") {
		if (!message.empty()) {
			boost::json::object obj;
			obj["msg"] = message;
			//client->sendMessage(message);
			client->call(IPC::Request("/privet/", IPC::Request::Type::POST, std::move(obj)),
				[](IPC::Request&& req) {
					std::cout << "Ответ пришел: " << req.m_data << std::endl;
				}
			);
		}
	}
}

int main()
{
	try {
		boost::asio::io_service service;

		// Создание клиента и подключение к серверу
		auto cl = std::make_shared<IPC::Client>(service, "test1.com");

		std::thread th(read_input, cl);

		service.run();

		th.join();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
	}
	return 0;
}