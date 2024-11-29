#include "server.h"

#include <unistd.h>	// unlink
#include <iostream> // cout

IPC::Session::Session(stream_protocol::socket socket)
	: m_socket(std::move(socket))
{
	std::cout << "Сессия создана" << std::endl;
}

IPC::Session::~Session()
{
	std::cout << "закрытие сессии" << std::endl;
}

void IPC::Session::startSession()
{
	doRead();
}

void IPC::Session::doRead()
{
	m_socket.async_read_some(buffer(m_data, max_length),
		[this, self = shared_from_this()](boost::system::error_code ec, std::size_t length) {
			if (!ec) {
				// TODO исправить это. Тут должна начинаться обработка
				// полученных данных и формирование ответа, а не это.
				// Пока что тут просто эхосервер
				std::cout << "получено: " << std::string(m_data, length) << std::endl;
				doWrite(length); // Echo back the received data
			}
			else if (length == 0)
			{
				std::cout << "чтение окончено, клиент отчключился" << std::endl;
			}
			else
			{
				std::cerr << "ошибка при чтении: " << ec.message() << std::endl;
			}
		});
}

void IPC::Session::doWrite(std::size_t length)
{
	// асинхронно отправляем буфер, который считали
	async_write(m_socket, buffer(m_data, length),
		[this, self = shared_from_this()](boost::system::error_code ec, std::size_t length) {
			if (!ec)
			{
				std::cout << "отправлено: " << m_data << std::endl;
				doRead(); // продолжамем читать
			}
			else
			{
				std::cerr << "ошибка при записи: " << ec.message() << "\n";
			}
		});
}

// -----------------------------------------------

IPC::Server::Server(io_service& service, const std::string& endpoint)
	:m_endpoint(endpoint), m_service(service)
	//"ripc/" + 
{
	std::cout << "Запуск сервера\n";

	// закрытие сокета
	unlink(m_endpoint.c_str());

	// создание объекта, принимающего соединения
	m_acceptor = std::make_unique<local::stream_protocol::acceptor>(
		m_service, local::stream_protocol::endpoint(m_endpoint)
	);

	// запуск сервера
	startAccepting();
}

void IPC::Server::startAccepting()
{
	auto new_session = std::make_shared<Session>(stream_protocol::socket(m_service));
	m_acceptor->async_accept(new_session->m_socket,
		[this, new_session](const boost::system::error_code& er)
		{
			std::cout << "Кто-то подключается" << std::endl;
			handleSession(new_session, er);
		}
	);
}

void IPC::Server::handleSession(
	std::shared_ptr<Session> new_session,
	const boost::system::error_code& er)
{
	// если нет ошибки
	if (!er)
	{
		std::cout << "Сессия запущена" << std::endl;
		// запускаем сессию
		new_session->startSession();
	}
	else
	{
		std::cerr << "сессия не запущена: " << er.message() << std::endl;
	}

	// принимаем новые подклчючения
	startAccepting();
}

