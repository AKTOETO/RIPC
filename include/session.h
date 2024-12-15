#ifndef SESSION_H
#define SESSION_H

#include <string>
#include <memory>
#include <functional>

#include <boost/asio.hpp>
#include <boost/json.hpp>
using namespace boost::asio;
using namespace boost::asio::local;
using namespace boost::system;

namespace IPC
{
	// Класс, описывающий сессию с одним клиентом
	class Session : public std::enable_shared_from_this<Session>
	{
	public:
		Session(stream_protocol::socket&& socket);
		Session(stream_protocol::socket&& socket,
			std::function<void(std::shared_ptr<Session>, boost::json::value&&)> callback);
		~Session();

		// запуск обработки клиента
		void start();

		// пишем клиенту
		void send(const boost::json::value& json_data);
		void send(boost::json::value&& json_data);

		// установка колбэка для вызова со считанными данными
		void setOnReadCallback(
			std::function<void(std::shared_ptr<Session>, boost::json::value&&)> callback);
	private:

		// запуск чтения данных от клиента
		void doRead();

		// обработка после чтения
		void onRead(const error_code& err, std::size_t bytes_transferred);

		stream_protocol::socket m_socket;
		streambuf m_read_buffer;
		std::function<void(std::shared_ptr<Session>, boost::json::value&&)> m_onReadCallback;
	};
}

#endif // !SESSION_H