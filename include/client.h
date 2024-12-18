#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <memory>
#include <string>
#include <map>

#include <boost/asio.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

#include "session.h"
#include "request.h"
#include "response.h"

namespace IPC
{

	class Client : public std::enable_shared_from_this<Client>
	{
	public:
		Client(io_service& service, const std::string& endpoint);
		[[deprecated]] void sendMessage(const std::string&);
		// создание запроса
		void call(Request&& req, std::function<void(IPC::Response&&)>&& callback);

	private:
		void handleJson(std::shared_ptr<Session> session, boost::json::value&& json);
		void connect(const std::string& endpoint);

		stream_protocol::socket m_socket;
		std::shared_ptr<Session> m_session;
		//std::set<Request> m_requests;	// очередь запросов, на которые еще не пришел ответ
		//std::map<std::string, Request> m_requests;
		std::map<Request::IdType, std::function<void(IPC::Response&&)>> m_requests;
	};

} // namespace IPC



#endif // !CLIENT_H