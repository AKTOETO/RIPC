#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <memory>
#include <string>

#include <boost/asio.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

#include "session.h"

namespace IPC
{
	class Client : public std::enable_shared_from_this<Client>
	{
	public:
		Client(io_service& service, const std::string& endpoint);
		void sendMessage(const std::string&);

	private:
		void handleJson(std::shared_ptr<Session> session, boost::json::value&& json);
		void connect(const std::string& endpoint);

		stream_protocol::socket m_socket;
		std::shared_ptr<Session> m_session;
	};

} // namespace IPC


#endif // !CLIENT_H