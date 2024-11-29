#include "server.h"

#include <iostream>

int main()
{
	try{
		io_service serv;
		IPC::Server sr(serv, "/tmp/test1.com");
		serv.run();

		//std::cin.get();

	} catch (std::exception& e)
	{
		std::cerr <<"исключение: "<< e.what() << std::endl;
	}
	return 0;
}