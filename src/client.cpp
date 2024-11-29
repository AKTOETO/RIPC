#include "client.h"

IPC::Client::Client(io_service& service, const std::string& endpoint)
	: m_socket(service)
{
	connect(endpoint);
}

void IPC::Client::sendMessage(const std::string& msg)
{
	auto self(shared_from_this());
	std::cout<<"sending msg\n";
	async_write(m_socket, buffer(msg),
		[this, self](boost::system::error_code ec, std::size_t /*length*/) {
			if (!ec) {
				std::cout << "Сообщение отправлено" << std::endl;
				doRead(); // После отправки сообщения начинаем читать ответ
			}
			else {
				std::cerr << "Error on send: " << ec.message() << std::endl;
			}
		});
}

void IPC::Client::doRead()
{
	auto self(shared_from_this());
	m_socket.async_read_some(buffer(m_data, max_length),
		[this, self](boost::system::error_code ec, std::size_t length) {
			if (!ec) {
				std::cout << "Received: " << std::string(m_data, length) << std::endl;
			}
			else {
				std::cerr << "Error on receive: " << ec.message() << std::endl;
			}
		});
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
	}
}
