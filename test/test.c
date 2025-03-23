#include "ripc.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(const char *prog_name)
{
    printf("Usage:\n");
    printf("  %s [-rs] --register-server <server_name>\n", prog_name);
    printf("  %s [-rc] --register-client\n", prog_name);
}

int main(int argc, char *argv[])
{
    int fd;
    int ret = 0;

    // полезная инфа для драйвера
    int client_id = -1;
    struct server_registration reg =
        {
            .name = "my server",
            .server_id = -1};

    // обработка команд
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    // Открываем устройство
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    // Обработка команд
    if (strcmp(argv[1], "--register-server") == 0 || strcmp(argv[1], "-rs") == 0)
    {
        // имя сервера по умолчанию
        if (argc == 3)
            strncpy(reg.name, argv[2], MAX_SERVER_NAME - 1);

        if (ioctl(fd, IOCTL_REGISTER_SERVER, &reg) < 0)
        {
            perror("ioctl REGISTER_SERVER");
            ret = 1;
        }
        else
        {
            printf("Server '%d:%s' registered successfully\n", reg.server_id, reg.name);
        }
    }
    else if (strcmp(argv[1], "--register-client") == 0 || strcmp(argv[1], "-rc") == 0)
    {

        if (ioctl(fd, IOCTL_REGISTER_CLIENT, &client_id) < 0)
        {
            perror("ioctl REGISTER_CLIENT");
            ret = 1;
        }
        else
        {
            printf("Client registered successfully. ID: %d\n", client_id);
        }
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        ret = 1;
    }

    close(fd);
    return ret;
}