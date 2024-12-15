#include "server.h"

#include <unistd.h>	// unlink
#include <iostream> // cout

IPC::Server::Server(io_service& service, const std::string& endpoint)
	:m_endpoint("/tmp/ripc_" + endpoint), m_service(service)
{
	std::cout << "[Server]: Запуск сервера\n"
		<< "[Server]: Endpoint: " << m_endpoint.c_str() << std::endl;

	// закрытие сокета
	unlink(m_endpoint.c_str());

	// создание объекта, принимающего соединения
	m_acceptor = std::make_unique<local::stream_protocol::acceptor>(
		m_service, local::stream_protocol::endpoint(m_endpoint)
	);
}

void IPC::Server::serve()
{
	m_acceptor->async_accept([this](const error_code& err, local::stream_protocol::socket socket)
		{
			if (!err)
			{
				std::make_shared<Session>(std::move(socket),
					std::bind(&Server::handleJson, this,
						std::placeholders::_1, std::placeholders::_2))->start();
			}
			else
			{
				std::cerr << "[Server]: Ошибка при подключении клиента: " << err.message();
			}
			serve();
		});
}

void IPC::Server::handleJson(std::shared_ptr<Session> session, boost::json::value&& json)
{
	// TODO переделать это все. ЭТО ТЕСТОВЫЙ ВАРИАНТ
	auto str = boost::json::serialize(json);
	std::cout << "[Server]: Получено сообщение от клиента: " << str << std::endl;

	// Преобразуем boost::json::value в boost::json::object
	if (json.is_object()) {
		boost::json::object& obj = json.as_object();

		// Добавляем новую строку
		obj["from"] = "server";

		session->send(std::move(json));
	}
	else {
		std::cerr << "[Server]: Provided value is not a JSON object." << std::endl;
	}

}

