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
    
    // структура запроса регистрации сервера
    struct server_registration reg = {
        .name = "my server",
        .server_id = -1
    };

    // выбор названия
    if(argc >= 2)
    {
        strncpy(reg.name, argv[1], MAX_SERVER_NAME - 1);
        reg.name[MAX_SERVER_NAME - 1] = '\0';
    }

    if (ioctl(fd, IOCTL_REGISTER_SERVER, reg.name) < 0)
    {
        perror("ioctl REGISTER_SERVER");
        close(fd);
        return 1;
    }

    printf("Server '%d:%s' registered successfully\n", reg.server_id, reg.name);
    close(fd);
    return 0;
}