#include "server.h"
#include "request.h"
#include "response.h"

#include <unistd.h> // unlink
#include <iostream> // cout

#include <chrono>
#include <thread>
using namespace std::chrono_literals;

IPC::Server::Server(io_service &service, const std::string &endpoint)
	: m_endpoint("/tmp/ripc_" + endpoint), m_service(service)
{
	std::cout << "[Server]: Запуск сервера\n"
			  << "[Server]: Endpoint: " << m_endpoint.c_str() << std::endl;

	// закрытие сокета
	unlink(m_endpoint.c_str());

	// создание объекта, принимающего соединения
	m_acceptor = std::make_unique<local::stream_protocol::acceptor>(
		m_service, local::stream_protocol::endpoint(m_endpoint));
}

void IPC::Server::serve()
{
	m_acceptor->async_accept([this](const error_code &err, local::stream_protocol::socket socket)
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
			serve(); });
}

void IPC::Server::on(Request::Type type, const std::string &url,
					 std::function<void(const IPC::Request &, IPC::Response &)> callback)
{
	std::cout << "[Server]: добавляем обработчик [" << (int)type << " : " << url << "]\n";
	m_handlers.insert_or_assign({type, url}, callback);
}

void IPC::Server::on(const std::string &url, std::function<void(const IPC::Request &, IPC::Response &)> callback)
{
	std::cout << "[Server]: добавляем обработчик [ANY : " << url << "]\n";
	m_handlers.insert_or_assign({Request::Type::ANY, url}, callback);
}

void IPC::Server::handleJson(std::shared_ptr<Session> session,
							 boost::json::value &&json)
{
	std::cout << "[Server]: Получено сообщение от клиента: " << json << std::endl;

	// запуск обработки запроса
	try
	{
		// корнвертируем json в Request
		auto req = IPC::Request::fromJson(json);

		// создаем объект ответа клиенту
		Response res(req.m_id, req.m_data);

		// запуск обработки запроса
		handleRequest(req, res);

		// Отправка ответа клиенту
		session->send(IPC::Response::toJson(res));
	}
	catch (const std::exception &e)
	{
		std::cerr << "[Server]: Ошибка при обработке запроса: " << e.what() << '\n';
	}
}

void IPC::Server::handleRequest(Request &req, Response &res)
{
	std::cout << "[Server]: ищем обработчик" << std::endl;
	// ищем обработчик
	auto it = m_handlers.find({req.m_type, req.m_url});

	// если нашли, то вызываем обработчик
	if (it != m_handlers.end())
		it->second(req, res);
	// иначе выводим сигнализируем об ошибке
	else
	{
		auto &obj = res.m_data.as_object();
		obj["error"] = "обработчик не был найден";
		std::cout << "[Server]: обработчик не найден" << std::endl;
	}
}
