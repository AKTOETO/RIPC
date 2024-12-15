#ifndef HEADER_H
#define HEADER_H

#include <string>
#include <memory>

#include <boost/asio.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

#include "session.h"

namespace IPC
{
	//class Server;

	// // Класс, описывающий сессию с одним клиентом
	// class Session : public std::enable_shared_from_this<Session>
	// {
	// 	friend Server;
	// public:
	// 	Session(stream_protocol::socket socket);
	// 	~Session();

	// 	// запуск обработки клиента
	// 	void startSession();
	// private:

	// 	// обработка получения данных от клиента
	// 	void doRead();

	// 	// обработка отправки данных клиенту
	// 	void doWrite(std::size_t length);

	// 	stream_protocol::socket m_socket;
	// 	// enum { max_length = 1024 };
	// 	// char m_data[max_length];
	// 	streambuf m_buffer;
	// };

	class Server
	{
	public:
		/// @brief Создание сервера от имени сокета
		/// @param endpoint имя сокета
		Server(io_service& service, const std::string& endpoint);

		// запуск сервера
		void serve();

	private:
		// обработка сообщения от клиента
		void handleJson(std::shared_ptr<Session> session, boost::json::value&& json);

		std::string m_endpoint;
		io_service& m_service;
		std::unique_ptr<local::stream_protocol::acceptor> m_acceptor;
	};

} // namespace IPC


#endif // !HEADER_H