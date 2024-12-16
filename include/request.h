#ifndef REQUEST_H
#define REQUEST_H

#include <iostream>

#include <boost/asio.hpp>
#include <boost/json.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

namespace IPC
{
	struct Request
	{
		// тип данных для идентификатора каждого запроса
		using IdType = uint64_t;

		Request(IdType id);
		Request(const Request&) = delete;
		Request(Request&& req);
		Request& operator=(Request&& req);

		Request(std::string&& url,
			boost::json::value&& data = {});
		Request(const std::string& url,
			const boost::json::value& data = {});
		// установка нового id 
		static IdType getNextId()
		{
			// если индекс нового запроса превы
			if(g_max_id >= std::numeric_limits<IdType>::max() - 1)
			{
				g_max_id = 0;
			}
			return g_max_id++; 
		}

		bool operator<(const Request& req)
		{
			return m_id < req.m_id;
		}

		static boost::json::value toJson(const Request& req)
		{
			boost::json::object obj;
			try
			{
				obj["uid"] = boost::json::value(req.m_id);
				obj["url"] = req.m_url;
				obj["data"] = req.m_data;
			}
			catch(const std::exception& e)
			{
				std::cerr << "[Request]: Ошибка десериализации: " << e.what() << '\n';
			}
			
			return obj;
		}

		static Request fromJson(const boost::json::value& json)
		{
			if (!json.is_object())
			{
				std::cerr << "[Request]: переданный json не является объектом: " << json << std::endl;
				return Request("", nullptr);
			}
			
			std::cout<<"[Request]: Пытаемся преобразовать объект запроса из json"<<std::endl;

			const auto& obj = json.as_object();

			try
			{

				Request req(obj.at("uid").to_number<IdType>());
				req.m_url = obj.at("url").as_string();
				req.m_data = obj.at("data");

				std::cout<<"[Request]: Объект преобразован из json"<<std::endl;
				return req;				
			}
			catch(const std::exception& e)
			{
				std::cerr << "[Request]: Ошибка сериализации: " << e.what() << '\n';
			}
			return Request(-1);
		}

		//static uint32_t m_index;
		IdType m_id;
		std::string m_url;
		boost::json::value m_data;
		static IdType g_max_id;
	};
}

#endif // !REQUEST_H