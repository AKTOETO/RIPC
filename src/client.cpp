#include "client.h"

IPC::Client::Client(io_service& service, const std::string& endpoint)
	: m_socket(service)
{
	connect(endpoint);
}

void IPC::Client::sendMessage(const std::string& msg)
{
	boost::json::value json = { {"client", "hello"}, {"data", msg} };
	m_session->send(std::move(json));
}

void IPC::Client::call(Request req)
{
	// добавление запроса в множество
	//m_requests.insert(req);
	m_session->send(Request::toJson(req));
	m_requests[req.m_url] = std::move(req);
}

void IPC::Client::handleJson(std::shared_ptr<Session> session, boost::json::value&& json)
{
	std::cout << "[Client]: что-то пришло:\n" << json << std::endl;

	// получаем структуру запроса:
	Request req = Request::fromJson(std::move(json));

	// ищем запрос для запуска обработчика ответа
	auto it = m_requests.find(req.m_url);
	if (it != m_requests.end())
	{
		std::cout << "[Client]: Обработчик найден: " << req.m_url << std::endl;
		it->second();
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

IPC::Request::Request()
	:m_url("-empty"), m_data({}), m_onReply(nullptr)
{
	//m_index = m_index
}

IPC::Request::Request(Request&& req)
	:m_data(std::move(req.m_data)),
	m_onReply(std::move(req.m_onReply)),
	m_url(std::move(req.m_url))
{
}

IPC::Request& IPC::Request::operator=(Request&& req)
{
	if (this != &req)
	{
		m_data = std::move(req.m_data);
		m_onReply = std::move(req.m_onReply);
		m_url = std::move(req.m_url);
	}
	return *this;
}

IPC::Request::Request(std::string&& url,
	std::function<void(boost::json::value&)> onReply,
	boost::json::value&& data)
	: m_url(std::move(url)), m_onReply(std::move(onReply)), m_data(std::move(data))
{
}

IPC::Request::Request(const std::string& url,
	std::function<void(boost::json::value&)> onReply,
	const boost::json::value& data)
	:m_url(std::move(url)), m_onReply(std::move(onReply)), m_data(std::move(data))
{
}

void IPC::Request::operator()()
{
	if (m_onReply)
		m_onReply(m_data);
	else
		std::cerr << "[Request]: обработчик не существует" << std::endl;
}
