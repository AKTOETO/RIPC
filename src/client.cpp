#include "client.h"

IPC::Client::Client(io_service& service, const std::string& endpoint)
	: m_socket(service)
{
	connect(endpoint);
}

void IPC::Client::sendMessage(const std::string& msg)
{
	boost::json::value json = {{"client", "hello"}, {"data", msg}};
	m_session->send(std::move(json));
}

void IPC::Client::handleJson(std::shared_ptr<Session> session, boost::json::value&& json)
{
	auto str = boost::json::serialize(json);
	std::cout << "[Client]: что-то пришло:\n" << str << std::endl;
}

void IPC::Client::connect(const std::string& endpoint)
{
	boost::system::error_code ec;
	m_socket.connect(stream_protocol::endpoint(endpoint), ec);
	if (ec) {
		std::cerr << "Error connecting to server: " << ec.message() << std::endl;
	}
	else {
		std::cout << "Connected to server!" << std::endl;
		// создаем сессия для общения с сервером
		m_session = std::make_shared<Session>(std::move(m_socket),
			std::bind(&Client::handleJson, this,
				std::placeholders::_1, std::placeholders::_2));
		m_session->start();
	}
}
