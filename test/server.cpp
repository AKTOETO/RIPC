#include "server.h"

#include <iostream>

int main()
{
	try{
		io_service serv;
		IPC::Server sr(serv, "test1.com");
		sr.serve();
		serv.run();
		

		//std::cin.get();

	} catch (std::exception& e)
	{
		std::cerr <<"исключение: "<< e.what() << std::endl;
	}
	return 0;
}