#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <memory>
#include <string>

#include <boost/asio.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

namespace IPC
{
	class Client : public std::enable_shared_from_this<Client>
	{
	public:
		Client(io_service& service, const std::string& endpoint);
		void sendMessage(const std::string&);

	private:
		void connect(const std::string& endpoint);
		void doRead();

		stream_protocol::socket m_socket;
		enum { max_length = 1024 };
		char m_data[max_length];
	};

} // namespace IPC


#endif // !CLIENT_H