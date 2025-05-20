#include <errno.h> // errno
#include <fcntl.h> // open
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h> // ioctl
#include <unistd.h>    // read, close

// Подключаем общий заголовочный файл проекта
#include "id_pack.h" // Для IS_ID_VALID, если он не включен в ripc.h
#include "ripc.h"    // Здесь должны быть все нужные определения

// Функция для печати состояния драйвера
void print_driver_state(const struct st_reg_tasks *state)
{
    if (!state)
    {
        printf("Error: Received null state data from driver.\n");
        return;
    }

    printf("====== RIPC Driver State ======\n");
    printf("Total registered tasks (processes): %d\n", state->tasks_count);
    printf("---------------------------------\n");

    for (int i = 0; i < state->tasks_count && i < MAX_PROCESSES; ++i)
    {
        const struct st_task *task = &state->tasks[i];
        printf("Task PID: %d\n", task->pid);

        if (task->pid == getpid())
        {
            printf("It is me. Monitor\n");
        }

        else
        {

            if (task->servers_count > 0)
            {
                printf("  Servers (%d):\n", task->servers_count);
                for (int j = 0; j < task->servers_count && j < MAX_SERVERS_PER_PID; ++j)
                {
                    const struct st_server *server = &task->servers[j];
                    printf("    Server ID: %d, Name: \"%.*s\"\n", server->id, MAX_SERVER_NAME - 1, server->name);
                    if (server->conn_count > 0)
                    {
                        printf("      Connected Client IDs (%d): ", server->conn_count);
                        for (int k = 0; k < server->conn_count && k < MAX_CLIENTS_PER_SERVER; ++k)
                        {
                            printf("%d ", server->conn_ids[k]);
                        }
                        printf("\n");
                    }
                    else
                    {
                        printf("      No connected clients.\n");
                    }
                }
            }
            else
            {
                printf("  No servers registered by this task.\n");
            }

            if (task->clients_count > 0)
            {
                printf("  Clients (%d):\n", task->clients_count);
                for (int j = 0; j < task->clients_count && j < MAX_CLIENTS_PER_PID; ++j)
                {
                    const struct st_client *client = &task->clients[j];
                    printf("    Client ID: %d", client->id);
                    if (IS_ID_VALID(client->srv_id))
                    {
                        printf(" (Connected to Server ID: %d)\n", client->srv_id);
                    }
                    else
                    {
                        printf(" (Not connected or srv_id N/A)\n");
                    }
                }
            }
            else
            {
                printf("  No clients registered by this task.\n");
            }
        }
        printf("---------------------------------\n");
    }
    if (state->tasks_count == 0)
    {
        printf("No tasks registered with the RIPC driver.\n");
    }
    printf("===============================\n");
}

int main()
{
    int fd;
    struct st_reg_tasks driver_state;
    ssize_t bytes_read;

    // Открываем устройство драйвера
    printf("Opening device %s...\n", DEVICE_PATH);
    fd = open(DEVICE_PATH, O_RDWR); // O_RDWR, если ioctl требует прав на запись
    if (fd < 0)
    {
        perror("Failed to open device");
        return 1;
    }
    printf("Device opened successfully (fd=%d).\n", fd);

    // Отправляем IOCTL_REGISTER_MONITOR
    printf("Registering as monitor (sending IOCTL_REGISTER_MONITOR)...\n");
    if (ioctl(fd, IOCTL_REGISTER_MONITOR, NULL) < 0)
    {
        perror("Failed to register as monitor (ioctl)");
        close(fd);
        return 1;
    }
    else
    {
        printf("Successfully registered as monitor.\n");
    }

    // Читаем данные в структуру st_reg_tasks
    printf("Reading driver state...\n");
    memset(&driver_state, 0, sizeof(driver_state)); // Обнуляем структуру перед чтением

    bytes_read = read(fd, &driver_state, sizeof(driver_state));

    if (bytes_read < 0)
    {
        perror("Failed to read driver state");
        close(fd);
        return 1;
    }

    if ((size_t)bytes_read == 0)
    {
        printf("No data read from driver. Is the monitor mode active or any data available?\n");
    }
    else if ((size_t)bytes_read < sizeof(driver_state))
    {
        fprintf(stderr, "Warning: Read incomplete data. Expected %zu bytes, got %zd bytes. Displaying partial data.\n",
                sizeof(driver_state), bytes_read);
    }
    else
    {
        printf("Successfully read %zd bytes of driver state.\n", bytes_read);
    }

    // Выводим полученные данные (даже если прочитано меньше, пытаемся вывести то, что есть)
    if (bytes_read > 0)
    {
        print_driver_state(&driver_state);
    }

    // Закрываем устройство
    printf("Closing device...\n");
    if (close(fd) < 0)
    {
        perror("Failed to close device");
        // Не критично на этом этапе
    }
    printf("Device closed.\n");

    return 0;
}