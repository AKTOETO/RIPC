#include "ripc.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int fd = open("/dev/" DEVICE_NAME, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }
    char server_name[MAX_SERVER_NAME] = "my_server";

    // выбор названия
    if(argc >= 2)
    {
        strncpy(server_name, argv[1], MAX_SERVER_NAME - 1);
        server_name[MAX_SERVER_NAME - 1] = '\0';
    }

    if (ioctl(fd, IOCTL_REGISTER_SERVER, server_name) < 0)
    {
        perror("ioctl REGISTER_SERVER");
        close(fd);
        return 1;
    }

    printf("Server '%s' registered successfully\n", server_name);
    close(fd);
    return 0;
}