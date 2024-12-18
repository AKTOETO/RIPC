#include "server.h"

#include <iostream>

int main()
{
	try
	{
		io_service serv;
		IPC::Server sr(serv, "test1.com");

		sr.on("/privet/",
			  [](const IPC::Request &req, IPC::Response &res)
			  {
				  auto &data = res.m_data.as_object();
				  data["res"] = "im server";
			  });

		sr.serve();
		serv.run();

		// std::cin.get();
	}
	catch (std::exception &e)
	{
		std::cerr << "исключение: " << e.what() << std::endl;
	}
	return 0;
}