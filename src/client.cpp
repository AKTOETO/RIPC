#include "client.h"

#include <limits>

IPC::Client::Client(io_service& service, const std::string& endpoint)
	: m_socket(service)
{
	connect("/tmp/ripc_" + endpoint);
}

void IPC::Client::sendMessage(const std::string& msg)
{
	boost::json::value json = { {"client", "hello"}, {"data", msg} };
	m_session->send(std::move(json));
}

void IPC::Client::call(Request&& req, std::function<void(IPC::Request&&)>&& callback)
{
	// добавление запроса в множество
	//m_requests.insert(req);
	m_session->send(Request::toJson(req));
	m_requests.insert({req.m_id, std::move(callback)});
	//m_requests[req.m_url] = std::move(req);
}

void IPC::Client::handleJson(std::shared_ptr<Session> session, boost::json::value&& json)
{
	std::cout << "[Client]: что-то пришло:\n" << json << std::endl;

	// получаем структуру запроса:
	Request req = Request::fromJson(std::move(json));

	std::cout<<"[Client]: Ищем обработчик для запроса" <<std::endl;
	// ищем запрос для запуска обработчика ответа
	auto it = m_requests.find(req.m_id);
	if (it != m_requests.end())
	{
		std::cout << "[Client]: Обработчик найден. id = " << it->first << std::endl;
		it->second(std::move(req));
		m_requests.erase(it);
	}
	else
	{
		std::cerr << "[Client]: Обработчик не найден: " << req.m_url << std::endl;
	}
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
