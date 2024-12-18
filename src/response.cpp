#include "response.h"

IPC::Response::Response(IdType uid, const boost::json::value &data)
:m_uid(uid), m_data(data)
{
}

IPC::Response::Response(Response &&res)
    : m_data(std::move(res.m_data)),
      m_uid(std::move(res.m_uid))
{
}

IPC::Response &IPC::Response::operator=(Response &&res)
{
    if (this != &res)
    {
        m_data = std::move(res.m_data);
        m_uid = std::move(res.m_uid);
    }
    return *this;
}
