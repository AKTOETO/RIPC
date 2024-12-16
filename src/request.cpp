#include "request.h"


IPC::Request::IdType IPC::Request::g_max_id = 0;

IPC::Request::Request(IdType id)
	:m_url("-empty"), m_data({}), m_id(id)
{
	//m_index = m_index
}

IPC::Request::Request(Request&& req)
	:m_data(std::move(req.m_data)),
	m_url(std::move(req.m_url)),
	m_id(std::move(req.m_id))
{
}

IPC::Request& IPC::Request::operator=(Request&& req)
{
	if (this != &req)
	{
		m_data = std::move(req.m_data);
		m_url = std::move(req.m_url);
		m_id = std::move(req.m_id);
	}
	return *this;
}

IPC::Request::Request(std::string&& url,
	boost::json::value&& data)
	: m_url(std::move(url)), m_data(std::move(data)),
	m_id(Request::getNextId())
{
}

IPC::Request::Request(const std::string& url,
	const boost::json::value& data)
	:m_url(std::move(url)), m_data(std::move(data)),
	m_id(Request::getNextId())
{
}
