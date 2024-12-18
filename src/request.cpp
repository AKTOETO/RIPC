#include "request.h"

IPC::Request::IdType IPC::Request::g_max_id = 0;

IPC::Request::Request(IdType id, Type type)
	: m_url("-empty"), m_data({}), m_id(id), m_type(type)
{
}

IPC::Request::Request(Request &&req)
	: m_data(std::move(req.m_data)),
	  m_url(std::move(req.m_url)),
	  m_id(std::move(req.m_id)),
	  m_type(std::move(req.m_type))
{
}

IPC::Request &IPC::Request::operator=(Request &&req)
{
	if (this != &req)
	{
		m_data = std::move(req.m_data);
		m_url = std::move(req.m_url);
		m_id = std::move(req.m_id);
		m_type = std::move(req.m_type);
	}
	return *this;
}

IPC::Request::Request(std::string &&url,
					  Type type,
					  boost::json::value &&data)
	: m_url(std::move(url)), m_data(std::move(data)),
	  m_id(Request::getNextId()), m_type(type)
{
}

IPC::Request::Request(const std::string &url,
					  Type type,
					  const boost::json::value &data)
	: m_url(std::move(url)), m_data(std::move(data)),
	  m_id(Request::getNextId()), m_type(type)
{
}
