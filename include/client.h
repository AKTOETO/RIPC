#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <memory>
#include <string>
//#include <set>
#include <map>

#include <boost/asio.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

#include "session.h"

namespace IPC
{
	struct Request
	{
		Request();
		Request(const Request&) = delete;
		Request(Request&& req);
		Request& operator=(Request&& req);

		Request(std::string&& url, std::function<void(boost::json::value&)> onReply,
			boost::json::value&& data = {});
		Request(const std::string& url, std::function<void(boost::json::value&)> onReply,
			const boost::json::value& data = {});

		// запуск обработки ответа
		void operator()();

		// bool operator<(const Request& req)
		// {
		// 	return m_index < req.m_index;
		// }

		static boost::json::value toJson(const Request& req)
		{
			boost::json::object obj;
			obj["url"] = req.m_url;
			obj["data"] = req.m_data;
			return obj;
		}

		static Request fromJson(const boost::json::value& json)
		{
			if (!json.is_object())
			{
				std::cerr << "[Request]: переданный json не является объектом: " << json << std::endl;
				return Request("", nullptr);
			}

			const auto& obj = json.as_object();

			Request req;
			req.m_url = obj.at("url").as_string();
			req.m_data = obj.at("data");

			return req;
		}

		//static uint32_t m_index;
		std::string m_url;
		boost::json::value m_data;
		std::function<void(boost::json::value&)> m_onReply;
	};
	//uint32_t Request::m_index = 0;

	class Client : public std::enable_shared_from_this<Client>
	{
	public:
		Client(io_service& service, const std::string& endpoint);
		[[deprecated]] void sendMessage(const std::string&);
		// создание запроса
		void call(Request req);

	private:
		void handleJson(std::shared_ptr<Session> session, boost::json::value&& json);
		void connect(const std::string& endpoint);

		stream_protocol::socket m_socket;
		std::shared_ptr<Session> m_session;
		//std::set<Request> m_requests;	// очередь запросов, на которые еще не пришел ответ
		std::map<std::string, Request> m_requests;
	};

} // namespace IPC



#endif // !CLIENT_H