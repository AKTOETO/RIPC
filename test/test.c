#include "ripc.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_INPUT_LEN 256
#define DEVICE_PATH "/dev/" DEVICE_NAME

void print_commands()
{
    printf("\nAvailable commands:\n");
    printf("1. register-server <name>  - Register new server\n");
    printf("2. register-client         - Register new client\n");
    printf("3. connect <server_name>   - Connect client to server\n");
    printf("4. show [client|server]    - Show client or server data\n");
    printf("5. exit                    - Exit application\n");
}

int main()
{
    int fd;
    char input[MAX_INPUT_LEN];
    char *command, *arg;
    bool running = true;

    struct connect_to_server con = {0};
    struct server_registration reg = {0};

    // Открываем устройство
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    printf("RIPC Client Application\n");
    print_commands();

    while (running)
    {
        printf("\n> ");
        if (!fgets(input, MAX_INPUT_LEN, stdin))
            break;

        // Удаляем символ новой строки
        input[strcspn(input, "\n")] = 0;

        command = strtok(input, " ");
        arg = strtok(NULL, " ");

        if (!command)
            continue;

        if (strcmp(command, "register-server") == 0)
        {
            if (!arg)
            {
                printf("Server name required!\n");
                continue;
            }

            strncpy(reg.name, arg, MAX_SERVER_NAME - 1);
            reg.name[MAX_SERVER_NAME - 1] = '\0';

            if (ioctl(fd, IOCTL_REGISTER_SERVER, &reg) < 0)
            {
                perror("Server registration failed");
            }
            else
            {
                printf("Server '%s' registered with ID: %d\n", reg.name, reg.server_id);
            }
        }
        else if (strcmp(command, "register-client") == 0)
        {
            if (ioctl(fd, IOCTL_REGISTER_CLIENT, &con.client_id) < 0)
            {
                perror("Client registration failed");
            }
            else
            {
                printf("Client registered with ID: %d\n", con.client_id);
            }
        }
        else if (strcmp(command, "connect") == 0)
        {
            if (!arg)
            {
                printf("Server name required!\n");
                continue;
            }

            if (con.client_id == -1)
            {
                printf("Register client first!\n");
                continue;
            }

            strncpy(con.server_name, arg, MAX_SERVER_NAME - 1);
            con.server_name[MAX_SERVER_NAME - 1] = '\0';

            if (ioctl(fd, IOCTL_CONNECT_TO_SERVER, &con) < 0)
            {
                perror("Connection failed");
                con.server_name[0] = '\0';
            }
            else
            {
                printf("Client %d connected to server '%s'\n",
                       con.client_id, con.server_name);
            }
        }
        else if (strcmp(command, "show") == 0)
        {
            if (!arg)
            {
                printf("[client|server]!\n");
                continue;
            }

            if (strcmp(arg, "client") == 0)
            {
                printf("\tclient id: %d \n\tserver name: %s", con.client_id, con.server_name);
            }
            else if (strcmp(arg, "server") == 0)
            {
                printf("\tserver id: %d \n\tserver name: %s", reg.server_id, reg.name);
            }
            else
            {
                printf("Unknown type [%s]. Must be [client|server]", arg);
            }
        }
        else if (strcmp(command, "exit") == 0)
        {
            running = false;
        }
        else
        {
            printf("Unknown command: %s\n", command);
            print_commands();
        }
    }

    close(fd);
    printf("Application closed\n");
    return 0;
}