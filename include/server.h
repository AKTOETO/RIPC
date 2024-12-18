#ifndef HEADER_H
#define HEADER_H

#include <string>
#include <memory>
#include <functional>
#include <map>

#include <boost/asio.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

#include "session.h"
#include "request.h"
#include "response.h"

namespace IPC
{
	class Server
	{
	public:
		/// @brief Создание сервера от имени сокета
		/// @param endpoint имя сокета
		Server(io_service &service, const std::string &endpoint);

		// запуск сервера
		void serve();

		// регистрация обработчика
		void on(Request::Type type, const std::string& url,
				std::function<void(const IPC::Request &, IPC::Response &)> callback);
		void on(const std::string& url,
				std::function<void(const IPC::Request &, IPC::Response &)> callback);

	private:
		// обработка сообщения от клиента
		void handleJson(std::shared_ptr<Session> session, boost::json::value &&json);

		// обработка запроса зарегистрированным обработчиком
		void handleRequest(Request& req, Response& res);

		std::string m_endpoint;
		io_service &m_service;
		std::unique_ptr<local::stream_protocol::acceptor> m_acceptor;

		// список обработчиков
		std::map<std::pair<Request::Type, std::string>,
				 std::function<void(const IPC::Request &, IPC::Response &)>>
			m_handlers;
	};

} // namespace IPC

#endif // !HEADER_H