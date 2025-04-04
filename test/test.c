#include "ripc.h"    // Ваш заголовок с IOCTL и структурами
#include "id_pack.h" // Ваш заголовок для упаковки/распаковки ID
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h> // для mmap и макросов его
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#define MAX_INPUT_LEN 256
// #define DEVICE_PATH "/dev/" DEVICE_NAME // Предполагаем, что определено в ripc.h

// --- Константы для серверной части ---
#define MAX_SERVER_CLIENTS 16 // Макс. клиентов, которых может отслеживать сервер
#define MAX_SERVER_SHM 16     // Макс. областей памяти, отображаемых сервером
// ------------------------------------

// --- Структуры данных для сервера ---
// Информация о связи клиента и памяти
struct ClientConnectionInfo
{
    int client_id;
    int shm_id;
    bool active; // Активна ли эта запись
};

// Информация об отображенной сервером памяти
struct ServerShmMapping
{
    int shm_id;
    void *addr;
    size_t size;
    bool mapped; // Отображена ли эта память сервером
};
// ----------------------------------

// --- Структура данных для клиента ---
struct ClientShmInfo
{
    void *addr;
    size_t size;
    int client_id; // ID самого клиента
    bool mapped;
};
// ----------------------------------

// --- Глобальные переменные ---
volatile sig_atomic_t g_signal_received_flag = 0; // Флаг получения сигнала
volatile int g_signal_client_id = -1;             // ID клиента из последнего сигнала
volatile int g_signal_shm_id = -1;                // ID памяти из последнего сигнала

int g_registered_server_id = -1; // ID сервера, если процесс зарегистрирован как сервер
int g_registered_client_id = -1; // ID клиента, если процесс зарегистрирован как клиент

// Массивы для хранения состояния сервера
struct ClientConnectionInfo g_server_client_connections[MAX_SERVER_CLIENTS];
struct ServerShmMapping g_server_shm_mappings[MAX_SERVER_SHM];
int g_server_client_count = 0; // Количество активных клиентских подключений
int g_server_shm_count = 0;    // Количество активных отображений памяти сервером

// Состояние памяти для клиента
struct ClientShmInfo g_client_shm = {.mapped = false, .addr = MAP_FAILED, .client_id = -1};
// ------------------------------------------------------------------

// --- Вспомогательные функции для сервера ---

// Найти индекс подключения по client_id
int find_client_connection_index(int client_id)
{
    for (int i = 0; i < g_server_client_count; ++i)
    {
        if (g_server_client_connections[i].active && g_server_client_connections[i].client_id == client_id)
        {
            return i;
        }
    }
    return -1; // Не найдено
}

// Найти индекс отображения памяти по shm_id
int find_shm_mapping_index(int shm_id)
{
    for (int i = 0; i < g_server_shm_count; ++i)
    {
        // Note: Mappings might be added but not yet mapped
        if (g_server_shm_mappings[i].shm_id == shm_id)
        {
            return i;
        }
    }
    // If not found, maybe create a slot? Let's find or create.
    if (g_server_shm_count < MAX_SERVER_SHM)
    {
        int new_index = g_server_shm_count++;
        g_server_shm_mappings[new_index].shm_id = shm_id;
        g_server_shm_mappings[new_index].mapped = false;
        g_server_shm_mappings[new_index].addr = MAP_FAILED;
        g_server_shm_mappings[new_index].size = 0; // Will be set on successful mmap
        return new_index;
    }
    return -1; // Не найдено и нет места
}

// --- Обработчик сигнала ---
void signal_handler(int sig, siginfo_t *info, void *ucontext)
{

    if (sig == NEW_CONNECTION && info->si_code == SI_KERNEL)
    {
        u32 packed_data = (u32)info->si_int;
        g_signal_client_id = unpack_id1(packed_data);
        g_signal_shm_id = unpack_id2(packed_data);
        g_signal_received_flag = 1; // Устанавливаем флаг
    }
}
// -------------------------

void print_commands()
{
    printf("\nAvailable commands:\n");
    printf("  register-server <name>   - Register this process as a server\n");
    printf("  register-client          - Register this process as a client\n");
    printf("  connect <server_name>    - [Client] Connect client to server (initiates shm)\n");
    printf("  show client              - [Client] Show client data and mapping\n");
    printf("  show server              - [Server] Show server data, connections, and mappings\n");
    printf("  mmap client              - [Client] Map shared memory for the registered client\n");
    printf("  mmap server <shm_id>     - [Server] Map memory for specific shm_id (received via signal)\n");
    printf("  c_write <offset> <text>  - [Client] Write text to client mapped memory\n");
    printf("  c_read <offset> <len>    - [Client] Read from client mapped memory\n");
    printf("  s_write <client_id> <offset> <text> - [Server] Write to memory associated with client_id\n");
    printf("  s_read <client_id> <offset> <len>  - [Server] Read from memory associated with client_id\n");
    printf("  exit                     - Exit application\n");
    printf("Note: Server should wait for signal, then use 'mmap server <shm_id>'.\n");
}

// Инициализация серверных структур
void initialize_server_state()
{
    for (int i = 0; i < MAX_SERVER_CLIENTS; ++i)
    {
        g_server_client_connections[i].active = false;
        g_server_client_connections[i].client_id = -1;
        g_server_client_connections[i].shm_id = -1;
    }
    for (int i = 0; i < MAX_SERVER_SHM; ++i)
    {
        g_server_shm_mappings[i].mapped = false;
        g_server_shm_mappings[i].shm_id = -1;
        g_server_shm_mappings[i].addr = MAP_FAILED;
        g_server_shm_mappings[i].size = 0;
    }
    g_server_client_count = 0;
    g_server_shm_count = 0;
}

int main()
{
    int fd;
    char input[MAX_INPUT_LEN];
    char *command, *arg1, *arg2, *arg3;
    bool running = true;

    // Локальные структуры для IOCTL
    struct connect_to_server con_ioctl_data = {0};
    struct server_registration reg_ioctl_data = {0};

    initialize_server_state(); // Инициализируем массивы сервера

    // --- Установка обработчика сигнала ---
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(NEW_CONNECTION, &sa, NULL) == -1)
    {
        perror("Failed to set signal handler");
        return 1;
    }

    // Открываем устройство
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
        char dev_path_buf[128];
        snprintf(dev_path_buf, sizeof(dev_path_buf), "/dev/%s", DEVICE_NAME);
        perror(dev_path_buf);
        fprintf(stderr, "Make sure the '%s' module is loaded and you have permissions.\n", DEVICE_NAME);
        return 1;
    }

    // получение размера страниц
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size < 0)
    {
        perror("sysconf(_SC_PAGE_SIZE) failed");
        // Установить значение по умолчанию или выйти
        page_size = 4096;
        fprintf(stderr, "Warning: Using default page size %ld\n", page_size);
    }
    else
    {
        printf("System Page Size: %ld bytes\n", page_size);
    }

    printf("RIPC Test Application (PID: %d)\n", getpid());
    print_commands();

    while (running)
    {
        // --- Обработка полученного сигнала (если есть) ---
        if (g_signal_received_flag)
        {
            printf("\n[Main Loop] Signal Processed: NEW_CONNECTION received for client_id=%d, shm_id=%d\n",
                   g_signal_client_id, g_signal_shm_id);

            // Проверяем, есть ли уже такое подключение
            bool found = false;
            for (int i = 0; i < g_server_client_count; ++i)
            {
                if (g_server_client_connections[i].active &&
                    g_server_client_connections[i].client_id == g_signal_client_id &&
                    g_server_client_connections[i].shm_id == g_signal_shm_id)
                {
                    printf("  Connection info already exists.\n");
                    found = true;
                    break;
                }
            }

            // Если не найдено и есть место, добавляем
            if (!found && g_server_client_count < MAX_SERVER_CLIENTS)
            {
                int index = g_server_client_count++;
                g_server_client_connections[index].client_id = g_signal_client_id;
                g_server_client_connections[index].shm_id = g_signal_shm_id;
                g_server_client_connections[index].active = true;
                printf("  Added new client connection: client_id=%d -> shm_id=%d\n",
                       g_signal_client_id, g_signal_shm_id);
                // Также подготовим слот для маппинга памяти, если его еще нет
                find_shm_mapping_index(g_signal_shm_id);
            }
            else if (!found)
            {
                printf("  Cannot add new client connection: MAX_SERVER_CLIENTS limit reached.\n");
            }

            // Сбрасываем флаг и временные переменные сигнала
            g_signal_received_flag = 0;
            g_signal_client_id = -1;
            // g_signal_shm_id оставляем - он может понадобиться для mmap server
            // Лучше его не сбрасывать тут, а использовать напрямую из структуры client_connections
        }
        // ----------------------------------------------------

        printf("> ");
        fflush(stdout);

        if (!fgets(input, MAX_INPUT_LEN, stdin))
        {
            if (errno == EINTR)
            {
                errno = 0;
                clearerr(stdin);
                continue;
            }
            break;
        }

        input[strcspn(input, "\n")] = 0;

        command = strtok(input, " ");
        arg1 = strtok(NULL, " ");
        arg2 = strtok(NULL, " ");
        arg3 = strtok(NULL, ""); // Остаток строки

        if (!command)
            continue;

        // --- Обработка команд ---

        if (strcmp(command, "register-server") == 0)
        {
            if (!arg1)
            {
                printf("Server name required!\n");
                continue;
            }
            if (g_registered_server_id != -1)
            {
                printf("Server already registered (ID: %d)\n", g_registered_server_id);
                continue;
            }

            strncpy(reg_ioctl_data.name, arg1, MAX_SERVER_NAME - 1);
            reg_ioctl_data.name[MAX_SERVER_NAME - 1] = '\0';
            reg_ioctl_data.server_id = -1;

            if (ioctl(fd, IOCTL_REGISTER_SERVER, &reg_ioctl_data) < 0)
            {
                perror("Server registration failed");
            }
            else
            {
                g_registered_server_id = reg_ioctl_data.server_id;
                printf("Server '%s' registered with ID: %d\n", reg_ioctl_data.name, g_registered_server_id);
            }
        }
        else if (strcmp(command, "register-client") == 0)
        {
            if (g_registered_client_id != -1)
            {
                printf("Client already registered (ID: %d)\n", g_registered_client_id);
                continue;
            }
            int temp_client_id = -1;

            if (ioctl(fd, IOCTL_REGISTER_CLIENT, &temp_client_id) < 0)
            {
                perror("Client registration failed");
            }
            else
            {
                g_registered_client_id = temp_client_id;
                g_client_shm.client_id = g_registered_client_id; // Сохраняем для клиентской структуры
                printf("Client registered with ID: %d\n", g_registered_client_id);
            }
        }
        else if (strcmp(command, "connect") == 0)
        { // Клиентская команда
            if (!arg1)
            {
                printf("Server name required!\n");
                continue;
            }
            if (g_registered_client_id == -1)
            {
                printf("Register client first!\n");
                continue;
            }

            con_ioctl_data.client_id = g_registered_client_id;
            strncpy(con_ioctl_data.server_name, arg1, MAX_SERVER_NAME - 1);
            con_ioctl_data.server_name[MAX_SERVER_NAME - 1] = '\0';

            if (ioctl(fd, IOCTL_CONNECT_TO_SERVER, &con_ioctl_data) < 0)
            {
                perror("Connection failed");
            }
            else
            {
                printf("Client %d connect request sent for server '%s'.\n", g_registered_client_id, con_ioctl_data.server_name);
                printf("Client should now 'mmap client'. Server should wait for signal %d.\n", NEW_CONNECTION);
            }
        }
        else if (strcmp(command, "mmap") == 0)
        {
            if (!arg1)
            {
                printf("Specify [client|server <shm_id>]\n");
                continue;
            }

            if (strcmp(arg1, "client") == 0)
            { // Клиентский mmap
                if (g_registered_client_id == -1)
                {
                    printf("Register client first!\n");
                    continue;
                }
                if (g_client_shm.mapped)
                {
                    printf("Client memory already mapped at %p\n", g_client_shm.addr);
                    continue;
                }

                u32 packed_id = pack_ids(g_registered_client_id, 0);

                if (packed_id == (u32)-EINVAL)
                {
                    fprintf(stderr, "Failed to pack client ID for mmap offset.\n");
                    continue;
                }
                off_t offset_for_mmap = (off_t)packed_id * page_size;

                printf("Attempting client mmap with PAGE-ALIGNED offset: 0x%lx (packed_id=0x%x, page_size=%ld)\n",
                       (unsigned long)offset_for_mmap, packed_id, page_size);

                g_client_shm.addr = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset_for_mmap); // Используем новый offset

                if (g_client_shm.addr == MAP_FAILED)
                {
                    perror("Client mmap failed");
                    g_client_shm.mapped = false;
                    g_client_shm.addr = MAP_FAILED;
                }
                else
                {
                    g_client_shm.size = SHARED_MEM_SIZE;
                    g_client_shm.mapped = true;
                    printf("Client memory mapped at: %p\n", g_client_shm.addr);
                }
            }
            else if (strcmp(arg1, "server") == 0)
            { // Серверный mmap
                if (!arg2)
                {
                    printf("Usage: mmap server <shm_id>\n");
                    continue;
                }
                if (g_registered_server_id == -1)
                {
                    printf("Register server first!\n");
                    continue;
                }

                int shm_id_to_map = atoi(arg2);
                int map_index = find_shm_mapping_index(shm_id_to_map);

                if (map_index == -1)
                {
                    printf("SHM ID %d not found in received signals or limit reached.\n", shm_id_to_map);
                    continue;
                }
                else if (g_server_shm_mappings[map_index].mapped)
                {
                    printf("Server memory for shm_id %d already mapped at %p\n", shm_id_to_map, g_server_shm_mappings[map_index].addr);
                    continue;
                }

                u32 packed_id = pack_ids(g_registered_server_id, shm_id_to_map);
                if (packed_id == (u32)-EINVAL)
                {
                    fprintf(stderr, "Failed to pack server/shm IDs for mmap offset.\n");
                    continue;
                }
                off_t offset_for_mmap = (off_t)packed_id * page_size;

                printf("Attempting server mmap for shm_id %d with PAGE-ALIGNED offset: 0x%lx (packed_id=0x%x, page_size=%ld)\n",
                        shm_id_to_map, (unsigned long)offset_for_mmap, packed_id, page_size);

                g_server_shm_mappings[map_index].addr = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset_for_mmap); // Используем новый offset

                if (g_server_shm_mappings[map_index].addr == MAP_FAILED)
                {
                    perror("Server mmap failed");
                    // Do not mark as mapped
                    g_server_shm_mappings[map_index].addr = MAP_FAILED;
                    // Maybe remove the entry if creation failed? Or leave it as unmapped. Leaving is simpler.
                }
                else
                {
                    g_server_shm_mappings[map_index].size = SHARED_MEM_SIZE;
                    g_server_shm_mappings[map_index].mapped = true;
                    printf("Server memory for shm_id %d mapped at: %p\n", shm_id_to_map, g_server_shm_mappings[map_index].addr);
                }
            }
            else
            {
                printf("Unknown mmap type: %s. Use [client|server <shm_id>]\n", arg1);
            }
        }
        else if (strcmp(command, "show") == 0)
        {
            if (!arg1)
            {
                printf("Specify [client|server]\n");
                continue;
            }
            if (strcmp(arg1, "client") == 0)
            {
                printf("--- Client State (PID: %d) ---\n", getpid());
                printf("Registered Client ID: %d\n", g_registered_client_id);
                printf("Memory Mapped: %s\n", g_client_shm.mapped ? "Yes" : "No");
                if (g_client_shm.mapped)
                {
                    printf("  Address: %p\n", g_client_shm.addr);
                    printf("  Size: %zu bytes\n", g_client_shm.size);
                }
            }
            else if (strcmp(arg1, "server") == 0)
            {
                printf("--- Server State (PID: %d) ---\n", getpid());
                printf("Registered Server ID: %d\n", g_registered_server_id);
                printf("Connected Clients (%d/%d):\n", g_server_client_count, MAX_SERVER_CLIENTS);
                for (int i = 0; i < g_server_client_count; ++i)
                {
                    if (g_server_client_connections[i].active)
                    {
                        printf("  - Client ID: %d -> SHM ID: %d\n",
                               g_server_client_connections[i].client_id,
                               g_server_client_connections[i].shm_id);
                    }
                }
                printf("Mapped Shared Memories (%d/%d):\n", g_server_shm_count, MAX_SERVER_SHM);
                int mapped_count = 0;
                for (int i = 0; i < g_server_shm_count; ++i)
                {
                    // Only show details for successfully mapped regions
                    if (g_server_shm_mappings[i].mapped)
                    {
                        printf("  - SHM ID: %d -> Addr: %p, Size: %zu, Mapped: Yes\n",
                               g_server_shm_mappings[i].shm_id,
                               g_server_shm_mappings[i].addr,
                               g_server_shm_mappings[i].size);
                        mapped_count++;
                    }
                    else if (g_server_shm_mappings[i].shm_id != -1)
                    {
                        // Show entries that exist but aren't mapped (e.g., pending mmap)
                        printf("  - SHM ID: %d -> Mapped: No (Pending 'mmap server %d')\n",
                               g_server_shm_mappings[i].shm_id, g_server_shm_mappings[i].shm_id);
                    }
                }
                if (mapped_count == 0 && g_server_shm_count > 0)
                {
                    printf("  (No memory currently mapped by server)\n");
                }
                else if (g_server_shm_count == 0)
                {
                    printf("  (No shared memory tracked)\n");
                }
            }
            else
            {
                printf("Unknown type: %s. Use [client|server]\n", arg1);
            }
        }
        // --- Клиентские команды чтения/записи ---
        else if (strcmp(command, "c_write") == 0)
        {
            if (!g_client_shm.mapped)
            {
                printf("Client memory not mapped!\n");
                continue;
            }
            if (!arg1 || !arg2)
            {
                printf("Usage: c_write <offset> <text>\n");
                continue;
            }
            long offset = strtol(arg1, NULL, 0);
            char *text_to_write = arg2; // arg2 points to start of text
            if (offset < 0 || offset >= g_client_shm.size)
            {
                printf("Invalid offset %ld (max %zu)\n", offset, g_client_shm.size - 1);
                continue;
            }

            size_t text_len = strlen(text_to_write);
            size_t available_space = g_client_shm.size - offset;
            size_t write_len = (text_len < available_space) ? text_len : available_space;

            memcpy((char *)g_client_shm.addr + offset, text_to_write, write_len);
            if (write_len < available_space)
                ((char *)g_client_shm.addr)[offset + write_len] = '\0';
            printf("Client wrote %zu bytes at offset %ld\n", write_len, offset);
        }
        else if (strcmp(command, "c_read") == 0)
        {
            if (!g_client_shm.mapped)
            {
                printf("Client memory not mapped!\n");
                continue;
            }
            if (!arg1 || !arg2)
            {
                printf("Usage: c_read <offset> <length>\n");
                continue;
            }
            long offset = strtol(arg1, NULL, 0);
            long length = strtol(arg2, NULL, 0);
            if (offset < 0 || length <= 0 || offset >= g_client_shm.size)
            {
                printf("Invalid offset/length\n");
                continue;
            }
            if (offset + length > g_client_shm.size)
                length = g_client_shm.size - offset;

            char *read_buf = malloc(length + 1);
            if (!read_buf)
            {
                perror("malloc failed");
                continue;
            }
            memcpy(read_buf, (char *)g_client_shm.addr + offset, length);
            read_buf[length] = '\0';
            printf("Client read at offset %ld (%ld bytes): \"%s\"\n", offset, length, read_buf);
            free(read_buf);
        }
        // --- Серверные команды чтения/записи ---
        else if (strcmp(command, "s_write") == 0)
        {
            if (g_registered_server_id == -1)
            {
                printf("Register server first!\n");
                continue;
            }
            if (!arg1 || !arg2 || !arg3)
            {
                printf("Usage: s_write <client_id> <offset> <text>\n");
                continue;
            }
            int target_client_id = atoi(arg1);
            long offset = strtol(arg2, NULL, 0);
            char *text_to_write = arg3;

            // 1. Найти shm_id по client_id
            int conn_index = find_client_connection_index(target_client_id);
            if (conn_index == -1)
            {
                printf("Client ID %d not found in connections.\n", target_client_id);
                continue;
            }
            int target_shm_id = g_server_client_connections[conn_index].shm_id;

            // 2. Найти отображение по shm_id
            int map_index = find_shm_mapping_index(target_shm_id); // Should exist if conn exists
            if (map_index == -1 || !g_server_shm_mappings[map_index].mapped)
            {
                printf("Memory for SHM ID %d (client %d) is not mapped by server.\n", target_shm_id, target_client_id);
                continue;
            }

            struct ServerShmMapping *mapping = &g_server_shm_mappings[map_index];

            // 3. Выполнить запись
            if (offset < 0 || offset >= mapping->size)
            {
                printf("Invalid offset %ld (max %zu)\n", offset, mapping->size - 1);
                continue;
            }
            size_t text_len = strlen(text_to_write);
            size_t available_space = mapping->size - offset;
            size_t write_len = (text_len < available_space) ? text_len : available_space;

            memcpy((char *)mapping->addr + offset, text_to_write, write_len);
            if (write_len < available_space)
                ((char *)mapping->addr)[offset + write_len] = '\0';
            printf("Server wrote %zu bytes to shm_id %d (client %d) at offset %ld\n", write_len, target_shm_id, target_client_id, offset);
        }
        else if (strcmp(command, "s_read") == 0)
        {
            if (g_registered_server_id == -1)
            {
                printf("Register server first!\n");
                continue;
            }
            if (!arg1 || !arg2 || !arg3)
            {
                printf("Usage: s_read <client_id> <offset> <length>\n");
                continue;
            }
            int target_client_id = atoi(arg1);
            long offset = strtol(arg2, NULL, 0);
            long length = strtol(arg3, NULL, 0);

            // 1. Найти shm_id по client_id
            int conn_index = find_client_connection_index(target_client_id);
            if (conn_index == -1)
            {
                printf("Client ID %d not found in connections.\n", target_client_id);
                continue;
            }
            int target_shm_id = g_server_client_connections[conn_index].shm_id;

            // 2. Найти отображение по shm_id
            int map_index = find_shm_mapping_index(target_shm_id);
            if (map_index == -1 || !g_server_shm_mappings[map_index].mapped)
            {
                printf("Memory for SHM ID %d (client %d) is not mapped by server.\n", target_shm_id, target_client_id);
                continue;
            }

            struct ServerShmMapping *mapping = &g_server_shm_mappings[map_index];

            // 3. Выполнить чтение
            if (offset < 0 || length <= 0 || offset >= mapping->size)
            {
                printf("Invalid offset/length\n");
                continue;
            }
            if (offset + length > mapping->size)
                length = mapping->size - offset;

            char *read_buf = malloc(length + 1);
            if (!read_buf)
            {
                perror("malloc failed");
                continue;
            }
            memcpy(read_buf, (char *)mapping->addr + offset, length);
            read_buf[length] = '\0';
            printf("Server read from shm_id %d (client %d) at offset %ld (%ld bytes): \"%s\"\n", target_shm_id, target_client_id, offset, length, read_buf);
            free(read_buf);
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

    // --- Освобождение ресурсов ---
    if (g_client_shm.mapped && g_client_shm.addr != MAP_FAILED)
    {
        munmap(g_client_shm.addr, g_client_shm.size);
        printf("Client memory unmapped.\n");
    }
    printf("Unmapping server memories...\n");
    for (int i = 0; i < g_server_shm_count; ++i)
    {
        if (g_server_shm_mappings[i].mapped && g_server_shm_mappings[i].addr != MAP_FAILED)
        {
            printf("  Unmapping shm_id %d at %p\n", g_server_shm_mappings[i].shm_id, g_server_shm_mappings[i].addr);
            munmap(g_server_shm_mappings[i].addr, g_server_shm_mappings[i].size);
        }
    }

    close(fd);
    printf("Application closed\n");
    return 0;
}