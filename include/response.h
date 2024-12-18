#ifndef RESPONSE_H
#define RESPONSE_H

#include <iostream>

#include <boost/asio.hpp>
#include <boost/json.hpp>
using namespace boost::asio;
using namespace boost::asio::local;

namespace IPC
{
    struct Response
    {
        // тип данных для идентификатора каждого запроса
        using IdType = uint64_t;

        // информация, передаваемая клиенту
        boost::json::value m_data;
        IdType m_uid;

        Response(IdType uid, const boost::json::value &data = {});
        Response(Response &&res);
        Response &operator=(Response &&res);
        Response(const Response &) = delete;

        static boost::json::value toJson(const Response &req)
        {
            boost::json::object obj;
            try
            {
                obj["uid"] = boost::json::value(req.m_uid);
                obj["data"] = req.m_data;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[Response]: Ошибка десериализации: " << e.what() << '\n';
            }

            return obj;
        }

        static Response fromJson(const boost::json::value &json)
        {
            if (!json.is_object())
            {
                std::cerr << "[Response]: переданный json не является объектом: " << json << std::endl;
                return Response(0);
            }

            std::cout << "[Response]: Пытаемся преобразовать объект запроса из json" << std::endl;

            const auto &obj = json.as_object();

            try
            {
                Response req(obj.at("uid").to_number<IdType>());
                req.m_data = obj.at("data");

                std::cout << "[Response]: Объект преобразован из json" << std::endl;
                return req;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[Response]: Ошибка сериализации: " << e.what() << '\n';
            }
            return Response(0);
        }
    };
}

#endif // !RESPONSE_H