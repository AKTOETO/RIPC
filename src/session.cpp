#include "session.h"

#include <iostream>

namespace IPC
{
	Session::Session(stream_protocol::socket&& socket)
		:m_socket(std::move(socket)),
		m_onReadCallback(nullptr)
	{
		std::cout << "[Session]: Сессия создана" << std::endl;
	};
	Session::~Session() {}
	Session::Session(stream_protocol::socket&& socket,
		std::function<void(std::shared_ptr<Session>, boost::json::value&&)> callback)
		:m_socket(std::move(socket)), m_onReadCallback(callback)
	{
		std::cout << "[Session]: Сессия создана" << std::endl;
	};

	// запуск обработки клиента
	void Session::start()
	{
		doRead();
	}

	void Session::send(const boost::json::value& json_data)
	{
		send(std::move(json_data));
	}

	void Session::send(boost::json::value&& json_data)
	{
		std::string data = boost::json::serialize(json_data);
		async_write(m_socket, buffer(data + '\n'), [self = shared_from_this()]
		(const error_code& err, std::size_t)
			{
				if (!err)
					std::cout << "[Session]: Отправлены данные" << std::endl;
				if (err == error::bad_descriptor)
					std::cout << "[Session]: Сокет удален" << std::endl;
				else
					std::cerr << "[Session]: Ошибка при отправке данных: " << err.message() << std::endl;
			}
		);
	}

	void Session::setOnReadCallback(
		std::function<void(std::shared_ptr<Session>, boost::json::value&&)> callback)
	{
		m_onReadCallback = callback;
	}

	void Session::doRead()
	{
		std::cout << "[Session]: Начинаем чтение" << std::endl;
		async_read_until(m_socket, m_read_buffer, '\n', std::bind(&Session::onRead, shared_from_this(),
			std::placeholders::_1, std::placeholders::_2));
	}

	void Session::onRead(const error_code& err, std::size_t bytes_transferred)
	{
		if (!err)
		{
			std::cout << "[Session]: считаны данные" << std::endl;

			// Преобразование streambuf в строку
			std::istream is(&m_read_buffer);
			std::string data(
				(std::istreambuf_iterator<char>(is)),
				std::istreambuf_iterator<char>()
			);

			// Конвертация строки в объект boost::json
			boost::json::value json_data = boost::json::parse(data);

			// Вызов обработчика с JSON-данными
			if (m_onReadCallback)
				m_onReadCallback(shared_from_this(), std::move(json_data));
			else
				std::cerr << "[Session]: Нет колбэка на обработку считанного json" << std::endl;

			// Очищаем буфер для следующего чтения
			m_read_buffer.consume(bytes_transferred);

			// Запускаем следующее чтение
			doRead();
		}
		else if (err == error::eof)
		{
			std::cout << "[Session]: удаленная сторона отключилась" << std::endl;
			m_socket.close();
		}
		else
		{
			std::cerr << "[Session]: Ошибка при чтении: " << err.message() << std::endl;
		}
	}
}