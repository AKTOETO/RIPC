#include "test.h"
#include <ctype.h> // isspace()

// Инициализация
void initialize_instances()
{
    memset(g_servers, 0, sizeof(g_servers));
    memset(g_clients, 0, sizeof(g_clients));
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        g_servers[i].server_id = -1;
        g_clients[i].client_id = -1;
        g_clients[i].shm.addr = MAP_FAILED;
        // Инициализация массивов внутри структур сервера
        for (int j = 0; j < MAX_SERVER_CLIENTS; ++j)
            g_servers[i].connections[j].active = false;
        for (int j = 0; j < MAX_SERVER_SHM; ++j)
            g_servers[i].mappings[j].mapped = false;
    }
    g_num_servers = 0;
    g_num_clients = 0;
}

// Обработчик сигнала
void signal_handler(int sig, siginfo_t *info, void *ucontext)
{
    // убираем варнинг
    (void)ucontext;

    if (sig == NEW_CONNECTION && info->si_code == SI_KERNEL)
    {
        u32 packed_data = (u32)info->si_int;
        g_signal_client_id = unpack_id1(packed_data);
        g_signal_shm_id = unpack_id2(packed_data);
        g_signal_received_flag = 1;
    }
}

bool setup_signal_handler()
{
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = signal_handler; // Прежний обработчик сигнала
    sigemptyset(&sa.sa_mask);
    if (sigaction(NEW_CONNECTION, &sa, NULL) == -1)
    {
        perror("Failed to set signal handler");
        return false;
    }
    printf("Signal handler is set\n");
    return true;
}

bool open_device()
{
    g_dev_fd = open(DEVICE_PATH, O_RDWR);
    if (g_dev_fd < 0)
    {
        perror(DEVICE_PATH);
        fprintf(stderr, "Make sure the '%s' module is loaded and you have permissions.\n", DEVICE_NAME);
        return false;
    }
    printf("Device '%s' is opened\n", DEVICE_PATH);
    return true;
}

void get_page_size()
{
    g_page_size = sysconf(_SC_PAGE_SIZE);
    if (g_page_size < 0)
    {
        perror("sysconf(_SC_PAGE_SIZE) failed");
        g_page_size = 4096;
        fprintf(stderr, "Warning: Using default page size %ld\n", g_page_size);
    }
    else
    {
        printf("System Page Size: %ld bytes\n", g_page_size);
    }
}

// получение индекса из строки
bool parse_index(const char *arg, int *index, int max_val)
{
    // если нет строки, выходим
    if (!arg)
        return false;
    char *endptr;
    long val = strtol(arg, &endptr, 10);

    // если после индекса еще что-то есть
    // или индекс выходит за границы
    // то выходим
    if (*endptr != '\0' || val < 0 || val >= max_val)
    {
        return false;
    }
    *index = (int)val;
    return true;
}

struct ServerInstance *find_server_instance(int index)
{
    if (index < 0 || index >= MAX_INSTANCES)
    {
        printf("Error: Server index %d out of bounds [0-%d].\n", index, MAX_INSTANCES - 1);
        return NULL;
    }
    if (!g_servers[index].active)
    {
        printf("Error: Server instance %d is not active. Use 'register-server %d <name>' first.\n", index, index);
        return NULL;
    }
    return &g_servers[index];
}

struct ClientInstance *find_client_instance(int index)
{
    if (index < 0 || index >= MAX_INSTANCES)
    {
        printf("Error: Client index %d out of bounds [0-%d].\n", index, MAX_INSTANCES - 1);
        return NULL;
    }
    if (!g_clients[index].active)
    {
        printf("Error: Client instance %d is not active. Use 'register-client %d' first.\n", index, index);
        return NULL;
    }
    return &g_clients[index];
}

// Ищет или создает слот для маппинга SHM у *конкретного* сервера
int server_find_shm_mapping_index(struct ServerInstance *server, int shm_id)
{
    if (!server)
        return -1;

    // поиск памяти с shm_id
    for (int i = 0; i < server->num_mappings; ++i)
    {
        if (server->mappings[i].shm_id == shm_id)
        {
            return i;
        }
    }

    // Не найдено, пробуем добавить
    if (server->num_mappings < MAX_SERVER_SHM)
    {
        int new_index = server->num_mappings++;
        server->mappings[new_index].shm_id = shm_id;
        server->mappings[new_index].mapped = false;
        server->mappings[new_index].addr = MAP_FAILED;
        server->mappings[new_index].size = 0;
        return new_index;
    }
    printf("Server %d: Cannot track more SHM mappings (limit %d reached).\n", server->server_id, MAX_SERVER_SHM);
    return -1;
}

// Ищет индекс подключения по client_id у *конкретного* сервера
int server_find_connection_index(struct ServerInstance *server, int client_id)
{
    if (!server)
        return -1;
    for (int i = 0; i < server->num_connections; ++i)
    {
        if (server->connections[i].active && server->connections[i].client_id == client_id)
        {
            return i;
        }
    }
    return -1;
}

// Добавляет информацию о подключении к *конкретному* серверу
void server_add_connection(struct ServerInstance *server, int client_id, int shm_id)
{
    if (!server)
        return;

    // Проверка на дубликат
    for (int i = 0; i < server->num_connections; ++i)
    {
        if (server->connections[i].active &&
            server->connections[i].client_id == client_id &&
            server->connections[i].shm_id == shm_id)
        {
            printf("Server %d: Connection info (client %d -> shm %d) already exists.\n", server->server_id, client_id, shm_id);
            return;
        }
    }

    // Добавление нового
    if (server->num_connections < MAX_SERVER_CLIENTS)
    {
        int index = server->num_connections++;
        server->connections[index].client_id = client_id;
        server->connections[index].shm_id = shm_id;
        server->connections[index].active = true;
        printf("Server %d: Added connection: client %d -> shm %d\n", server->server_id, client_id, shm_id);
        // Убедимся, что для shm_id есть слот в маппингах
        server_find_shm_mapping_index(server, shm_id);
    }
    else
    {
        printf("Server %d: Cannot add connection (limit %d reached).\n", server->server_id, MAX_SERVER_CLIENTS);
    }
}

// Регистрация сервера
bool server_register(const char *name)
{
    // свободная ячейка
    struct ServerInstance *srv = NULL;

    // поиск свобойдной ячейки для сервера
    int id = 0;
    for (; id < MAX_INSTANCES; id++)
    {
        if (g_servers[id].active == false)
        {
            srv = &g_servers[id];
            break;
        }
    }

    // если не получилось найти свободжную ячейку,
    // выводим ошибку
    if (!srv)
    {
        printf("There is not enought space for one more server");
        return false;
    }

    // создаем структуру для запроса регистрации в драйвере
    struct server_registration reg_ioctl_data;
    strncpy(reg_ioctl_data.name, name, MAX_SERVER_NAME - 1);
    reg_ioctl_data.name[MAX_SERVER_NAME - 1] = '\0';
    reg_ioctl_data.server_id = -1;

    // отправляем запрос регистрации
    if (ioctl(g_dev_fd, IOCTL_REGISTER_SERVER, &reg_ioctl_data) < 0)
    {
        perror("Server registration failed");
        return false;
    }

    // если регистрация прошла успешно, записываем сервер в список серверов
    srv->server_id = reg_ioctl_data.server_id;
    strncpy(srv->name, reg_ioctl_data.name, MAX_SERVER_NAME);
    // srv->pid = getpid();
    srv->active = true;
    srv->num_connections = 0;
    srv->num_mappings = 0;
    g_num_servers++;
    printf("Server instance %d registered as '%s' with driver_ID: %d\n", id, srv->name, srv->server_id);
    return true;
}

// отображение памяти shm_id в сервер
bool server_mmap(int index, int shm_id)
{
    struct ServerInstance *server = find_server_instance(index);
    if (!server)
        return false;

    int map_index = server_find_shm_mapping_index(server, shm_id);
    if (map_index == -1)
    {
        printf("Server %d: Cannot find or create mapping slot for shm_id %d.\n", server->server_id, shm_id);
        return false;
    }
    if (server->mappings[map_index].mapped)
    {
        printf("Server %d: Memory for shm_id %d already mapped at %p\n", server->server_id, shm_id, server->mappings[map_index].addr);
        return true; // Уже сделано
    }

    u32 packed_id = pack_ids(server->server_id, shm_id);
    if (packed_id == (u32)-EINVAL)
    {
        fprintf(stderr, "Failed to pack server/shm IDs for mmap.\n");
        return false;
    }

    off_t offset_for_mmap = (off_t)packed_id * g_page_size;

    printf("Server %d: Attempting mmap for shm_id %d with offset 0x%lx (packed 0x%x)\n",
           server->server_id, shm_id, (unsigned long)offset_for_mmap, packed_id);

    server->mappings[map_index].addr = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, g_dev_fd, offset_for_mmap);

    if (server->mappings[map_index].addr == MAP_FAILED)
    {
        perror("Server mmap failed");
        server->mappings[map_index].addr = MAP_FAILED; // Убедимся что MAP_FAILED
        server->mappings[map_index].mapped = false;
        return false;
    }
    else
    {
        server->mappings[map_index].size = SHARED_MEM_SIZE;
        server->mappings[map_index].mapped = true;
        printf("Server %d: Memory for shm_id %d mapped at: %p\n", server->server_id, shm_id, server->mappings[map_index].addr);
        return true;
    }
}

bool server_write(int index, int client_id, long offset, const char *text)
{
    // поиск сервера
    struct ServerInstance *server = find_server_instance(index);
    if (!server || !text)
        return false;

    // поиск соединения с необходимым клиентом
    int conn_index = server_find_connection_index(server, client_id);
    if (conn_index == -1)
    {
        printf("Server %d: Client ID %d not found in connections.\n", server->server_id, client_id);
        return false;
    }
    // получение shm_id
    int target_shm_id = server->connections[conn_index].shm_id;

    // поиск общей памяти с id = target_shm_id
    int map_index = server_find_shm_mapping_index(server, target_shm_id);
    if (map_index == -1 || !server->mappings[map_index].mapped)
    {
        printf("Server %d: Memory for SHM ID %d (client %d) is not mapped.\n", server->server_id, target_shm_id, client_id);
        return false;
    }
    struct ServerShmMapping *mapping = &server->mappings[map_index];

    // проверка сдвига
    if (offset < 0 || (size_t)offset >= mapping->size)
    {
        printf("Invalid offset %ld (max %zu)\n", offset, mapping->size - 1);
        return false;
    }
    size_t text_len = strlen(text);
    size_t available_space = mapping->size - (size_t)offset;
    size_t write_len = (text_len < available_space) ? text_len : available_space;

    // TODO: временный вариант записи в отдельную память. ЧИСТО ДЛЯ ПРИМЕРА
    memcpy((char *)mapping->addr + offset, text, write_len);
    if (write_len < available_space)
        ((char *)mapping->addr)[offset + write_len] = '\0';
    printf("Server %d wrote %zu bytes to shm_id %d (client %d) at offset %ld\n", server->server_id, write_len, target_shm_id, client_id, offset);
    return true;
}

bool server_read(int index, int client_id, long offset, long length)
{
    // поиск сервера
    struct ServerInstance *server = find_server_instance(index);
    if (!server)
        return false;

    // поиск соединения
    int conn_index = server_find_connection_index(server, client_id);
    if (conn_index == -1)
    {
        printf("Server %d: Client ID %d not found in connections.\n", server->server_id, client_id);
        return false;
    }
    // получение shm_id
    int target_shm_id = server->connections[conn_index].shm_id;

    // поиск общей памяти с id = target_shm_id
    int map_index = server_find_shm_mapping_index(server, target_shm_id);
    if (map_index == -1 || !server->mappings[map_index].mapped)
    {
        printf("Server %d: Memory for SHM ID %d (client %d) is not mapped.\n", server->server_id, target_shm_id, client_id);
        return false;
    }
    struct ServerShmMapping *mapping = &server->mappings[map_index];

    // проверка сдвига
    if (offset < 0 || length <= 0 || (size_t)offset >= mapping->size)
    {
        printf("Invalid offset/length\n");
        return false;
    }

    size_t read_length = (size_t)length;
    if ((size_t)offset + read_length > mapping->size)
    {
        read_length = mapping->size - (size_t)offset;
        printf("Warning: Server read length truncated to %zu\n", read_length);
    }

    // TODO: временное решение для чтения в отдельную область памяти
    char *read_buf = malloc(read_length + 1);
    if (!read_buf)
    {
        perror("malloc failed");
        return false;
    }
    memcpy(read_buf, (char *)mapping->addr + offset, read_length);
    read_buf[read_length] = '\0';
    printf("Server %d read from shm_id %d (client %d) at offset %ld (%zu bytes): \"%s\"\n", server->server_id, target_shm_id, client_id, offset, read_length, read_buf);
    free(read_buf);
    return true;
}

void server_show(int index)
{
    struct ServerInstance *server = find_server_instance(index);
    if (!server)
        return;

    printf("--- Server Instance %d State (PID: %d)\n", index, getpid());
    printf("Name: '%s', Registered ID: %d\n", server->name, server->server_id);
    printf("Connected Clients (%d/%d):\n", server->num_connections, MAX_SERVER_CLIENTS);
    int active_conn = 0;
    for (int i = 0; i < server->num_connections; ++i)
    {
        if (server->connections[i].active)
        {
            printf("  - Client ID: %d -> SHM ID: %d\n", server->connections[i].client_id, server->connections[i].shm_id);
            active_conn++;
        }
    }
    if (active_conn == 0)
        printf("  (No active connections)\n");

    printf("Mapped Shared Memories (%d/%d):\n", server->num_mappings, MAX_SERVER_SHM);
    int active_map = 0;
    for (int i = 0; i < server->num_mappings; ++i)
    {
        // Show slot only if it has a valid shm_id assigned
        if (server->mappings[i].shm_id != -1)
        {
            printf("  - SHM ID: %d -> Addr: %p, Size: %zu, Mapped: %s\n",
                   server->mappings[i].shm_id,
                   server->mappings[i].mapped ? server->mappings[i].addr : (void *)-1, // Show address only if mapped
                   server->mappings[i].mapped ? server->mappings[i].size : 0,
                   server->mappings[i].mapped ? "Yes" : "No");
            if (server->mappings[i].mapped)
                active_map++;
        }
    }
    if (server->num_mappings == 0)
        printf("  (No SHM mappings tracked)\n");
    else if (active_map == 0)
        printf("  (No memory currently mapped by this server instance)\n");
}

void server_cleanup(struct ServerInstance *server)
{
    if (!server || !server->active)
        return;
    printf("Cleaning up server instance '%s' (ID %d)...\n", server->name, server->server_id);
    for (int i = 0; i < server->num_mappings; ++i)
    {
        if (server->mappings[i].mapped && server->mappings[i].addr != MAP_FAILED)
        {
            printf("  Unmapping shm_id %d at %p\n", server->mappings[i].shm_id, server->mappings[i].addr);
            munmap(server->mappings[i].addr, server->mappings[i].size);
            server->mappings[i].mapped = false;
            server->mappings[i].addr = MAP_FAILED;
        }
    }
    server->active = false; // Помечаем слот как неактивный
    g_num_servers--;
}

// Действия с клиентом
bool client_register()
{
    // свободная ячейка
    struct ClientInstance *clnt = NULL;

    // поиск свобойдной ячейки для сервера
    int id = 0;
    for (; id < MAX_INSTANCES; id++)
    {
        if (g_clients[id].active == false)
        {
            clnt = &g_clients[id];
            break;
        }
    }

    // если не получилось найти свободжную ячейку,
    // выводим ошибку
    if (!clnt)
    {
        printf("There is not enought space for one more client");
        return false;
    }

    // id клиента, который вернет драйвер после успешной регистрации
    int temp_client_id = -1;
    if (ioctl(g_dev_fd, IOCTL_REGISTER_CLIENT, &temp_client_id) < 0)
    {
        perror("Client registration failed");
        return false;
    }

    clnt->client_id = temp_client_id;
    // clnt->pid = getpid();
    clnt->active = true;
    clnt->shm.mapped = false;
    clnt->shm.addr = MAP_FAILED;
    clnt->connected_server_name[0] = '\0';
    g_num_clients++;
    printf("Client instance %d registered with ID: %d\n", id, clnt->client_id);
    return true;
}

bool client_connect(int index, const char *server_name)
{
    // поиск клиента с таким id
    struct ClientInstance *client = find_client_instance(index);
    if (!client || !server_name)
    {
        ERR("Client or server name is empty");
        return false;
    }

    // заполняем структуру для запроса соединения с сервером
    struct connect_to_server con_ioctl_data;
    con_ioctl_data.client_id = client->client_id;
    strncpy(con_ioctl_data.server_name, server_name, MAX_SERVER_NAME - 1);
    con_ioctl_data.server_name[MAX_SERVER_NAME - 1] = '\0';

    // отправялем запрос на подключение к серверу
    if (ioctl(g_dev_fd, IOCTL_CONNECT_TO_SERVER, &con_ioctl_data) < 0)
    {
        perror("Connection failed");
        client->connected_server_name[0] = '\0'; // Сбросить имя сервера при ошибке
        return false;
    }

    // если подключение успешно, сохраняем подключение
    strncpy(client->connected_server_name, server_name, MAX_SERVER_NAME); // Сохранить имя сервера
    printf("Client %d (instance %d): connect request sent for server '%s'.\n", client->client_id, index, server_name);
    printf("Client %d (instance %d): should now 'client %d mmap'. Server should wait for signal %d.\n",
           client->client_id, index, index, NEW_CONNECTION);
    return true;
}

bool client_mmap(int index)
{
    struct ClientInstance *client = find_client_instance(index);
    if (!client)
        return false;
    if (client->shm.mapped)
    {
        printf("Client instance %d memory already mapped at %p\n", index, client->shm.addr);
        return true;
    }

    u32 packed_id = pack_ids(client->client_id, 0);
    if (packed_id == (u32)-EINVAL)
    {
        fprintf(stderr, "Failed to pack client ID for mmap.\n");
        return false;
    }

    off_t offset_for_mmap = (off_t)packed_id * g_page_size;
    printf("Client %d (instance %d): Attempting mmap with offset 0x%lx (packed 0x%x)\n",
           client->client_id, index, (unsigned long)offset_for_mmap, packed_id);

    client->shm.addr = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, g_dev_fd, offset_for_mmap);

    if (client->shm.addr == MAP_FAILED)
    {
        perror("Client mmap failed");
        client->shm.mapped = false;
        client->shm.addr = MAP_FAILED;
        return false;
    }
    else
    {
        client->shm.size = SHARED_MEM_SIZE;
        client->shm.mapped = true;
        printf("Client instance %d memory mapped at: %p\n", index, client->shm.addr);
        return true;
    }
}

bool client_write(int index, long offset, const char *text)
{
    // поиск клиента с подходящим id
    struct ClientInstance *client = find_client_instance(index);
    if (!client || !text || !client->shm.mapped)
    {
        if (client && !client->shm.mapped)
            printf("Client instance %d memory not mapped!\n", index);
        return false;
    }

    // если сдвиг выходи за границы памяти, то выходим с ошибкой
    if (offset < 0 || (size_t)offset >= client->shm.size)
    {
        printf("Invalid offset %ld (max %zu)\n", offset, client->shm.size - 1);
        return false;
    }

    // вычисляем размер записываемого текста
    size_t text_len = strlen(text);
    size_t available_space = client->shm.size - (size_t)offset;
    size_t write_len = (text_len < available_space) ? text_len : available_space;

    // записываем текст
    memcpy((char *)client->shm.addr + offset, text, write_len);

    // если мы заполнили не все свободное пространство, то запишем символ конца строки
    if (write_len < available_space)
        ((char *)client->shm.addr)[offset + write_len] = '\0';
    printf("Client instance %d wrote %zu bytes at offset %ld\n", index, write_len, offset);
    return true;
}

bool client_read(int index, long offset, long length)
{
    // поиск нужного клиента
    struct ClientInstance *client = find_client_instance(index);
    if (!client || !client->shm.mapped)
    {
        if (client && !client->shm.mapped)
            printf("Client instance %d memory not mapped!\n", index);
        return false;
    }

    // если сдвиг или длина текста неверные
    if (offset < 0 || length <= 0 || (size_t)offset >= client->shm.size)
    {
        printf("Invalid offset/length\n");
        return false;
    }

    // обрезаем блину данных, если она вызоди за границу общей памяти
    size_t read_length = (size_t)length;
    if ((size_t)offset + read_length > client->shm.size)
    {
        read_length = client->shm.size - (size_t)offset;
        printf("Warning: Client read length truncated to %zu\n", read_length);
    }

    // TODO: временное решение необходимое для печати данных из общего буфера
    char *read_buf = malloc(read_length + 1);
    if (!read_buf)
    {
        perror("malloc failed");
        return false;
    }
    memcpy(read_buf, (char *)client->shm.addr + offset, read_length);
    read_buf[read_length] = '\0';
    printf("Client instance %d read at offset %ld (%zu bytes): \"%s\"\n", index, offset, read_length, read_buf);
    free(read_buf);
    return true;
}

void client_show(int index)
{
    struct ClientInstance *client = find_client_instance(index);
    if (!client)
        return;

    printf("--- Client Instance %d State (PID: %d)\n", index, getpid());
    printf("Registered ID: %d\n", client->client_id);
    printf("Connected to Server: '%s'\n", client->connected_server_name[0] ? client->connected_server_name : "(Not Connected)");
    printf("Memory Mapped: %s\n", client->shm.mapped ? "Yes" : "No");
    if (client->shm.mapped)
    {
        printf("  Address: %p\n", client->shm.addr);
        printf("  Size: %zu bytes\n", client->shm.size);
    }
}

void client_cleanup(struct ClientInstance *client)
{
    if (!client || !client->active)
        return;
    printf("Cleaning up client instance (ID %d)...\n", client->client_id);
    if (client->shm.mapped && client->shm.addr != MAP_FAILED)
    {
        printf("  Unmapping memory at %p\n", client->shm.addr);
        munmap(client->shm.addr, client->shm.size);
        client->shm.mapped = false;
        client->shm.addr = MAP_FAILED;
    }
    client->active = false; // Помечаем слот как неактивный
    g_num_clients--;
}

// Основной цикл и обработка команд
void print_commands()
{
    printf("\nAvailable commands (use index [0-%d]):\n", MAX_INSTANCES - 1);
    printf("\thelp\n");
    printf("\tclient   register\n");
    printf("\tclient   <client index>   connect        <server name>\n");
    printf("\tclient   <client index>   mmap\n");
    printf("\tclient   <client index>   show\n");
    printf("\tclient   <client index>   write           <offset>       <text>\n");
    printf("\tclient   <client index>   read            <offset>       <len>\n");
    printf("\tserver   register         <server name>\n");
    printf("\tserver   <server index>   mmap            <shm_id>\n");
    printf("\tserver   <server_index>   show\n");
    printf("\tserver   <server_index>   write           <client_id>     <offset>   <text>\n");
    printf("\tserver   <server_index>   read            <client_id>     <offset>   <length>\n");
}

void process_signal()
{
    if (g_signal_received_flag)
    {
        printf("\n[Main Loop] Signal Received: NEW_CONNECTION for client_id=%d, shm_id=%d\n",
               g_signal_client_id, g_signal_shm_id);

        // TODO: нужно уведомлять только тот сервер, кому предназначается подключение,
        // TODO: а не всех сразу 
        // Уведомляем все активные серверные экземпляры об этом сигнале
        int notified_servers = 0;
        for (int i = 0; i < MAX_INSTANCES; ++i)
        {
            if (g_servers[i].active)
            {
                printf("  Notifying server instance %d (ID %d)...\n", i, g_servers[i].server_id);
                server_add_connection(&g_servers[i], g_signal_client_id, g_signal_shm_id);
                notified_servers++;
            }
        }
        if (notified_servers == 0)
        {
            printf("  Warning: No active server instances found to process the signal.\n");
        }

        // Сбрасываем флаг и данные сигнала
        g_signal_received_flag = 0;
        g_signal_client_id = -1;
        g_signal_shm_id = -1; // Сбрасываем и shm_id здесь
    }
}

void process_command(char *input)
{
    // команда
    char *command = strtok(input, " \n\0");

    // аргументы команды
    char *arg1 = strtok(NULL, " \n\0");
    char *arg2 = strtok(NULL, " \n\0");
    char *arg3 = strtok(NULL, " \n\0");
    char *arg4 = strtok(NULL, "\n\0");

    // если пустая строка, выходим
    if (!command)
        return;

    int index = -1, index2 = -1; // Для индексов и ID
    long offset = -1, length = -1;

    /**
     * обработка блока команд клиента
     *
     * command  arg1            arg2     arg3           arg4
     * client   register
     * client   <client index>  connect  <server name>
     * client   <client index>  mmap
     * client   <client index>  show
     * client   <client index>  write    <offset>       <text>
     * client   <client index>  read     <offset>       <len>
     */
    if (strcmp(command, "client") == 0)
    {
        // регистрация клиента
        if (arg1 && strcmp(arg1, "register") == 0)
        {
            client_register();
        }
        // получение id клиента для последующих операций
        else if (parse_index(arg1, &index, MAX_INSTANCES))
        {
            // подключение к серверу
            if (arg2 && strcmp(arg2, "connect") == 0)
            {
                // если есть имя сервера, к которому подключаюсь
                if (arg3)
                    client_connect(index, arg3);
                else
                {
                    printf("Usage: client <client_index> connect <server_name>\n");
                    return;
                }
            }
            // отображение памяти
            else if (arg2 && strcmp(arg2, "mmap") == 0)
            {
                client_mmap(index);
            }
            // отображание информации о клиенте
            else if (arg2 && strcmp(arg2, "show") == 0)
            {
                client_show(index);
            }
            // запись в отображенную память
            else if (arg2 && strcmp(arg2, "write") == 0)
            {
                // если нет сдвига или текста
                if (!arg3 || !arg4)
                {
                    printf("Usage: client <index> write <offset> <text>\n");
                    return;
                }

                // получение сдвига
                offset = strtol(arg3, NULL, 10);

                // нужно передать сдвиг и текст, который надо записать
                client_write(index, offset, arg4);
            }
            // чтение из общей памяти
            else if (arg2 && strcmp(arg2, "read") == 0)
            {
                // если нет сдвига или длины текста
                if (!arg3 || !arg4)
                {
                    printf("Usage: client <index> read <offset> <len>");
                    return;
                }

                // получаем сдвиг
                offset = strtol(arg3, NULL, 0);
                // получаем длину
                length = strtol(arg4, NULL, 0);

                // читаем
                client_read(index, offset, length);
            }
            else
            {
                if (command && arg1 && arg2)
                    printf("Unknown command: '%s %s %s'\n", command, arg1, arg2);
                else
                    printf("Argument 1 or 2 is empty\n");
            }
        }
        else
        {
            if (command && arg1)
                printf("Unknown command: '%s %s'\n", command, arg1);
            else
                printf("Argument 1 is empty\n");
        }
    }
    /**
     * обработка блока команд сервера
     *
     * command  arg1             arg2            arg3            arg4
     * server   register         <server name>
     * server   <server index>   mmap            <shm_id>
     * server   <server_index>   show
     * server   <server_index>   write           <client_id>     <offset>   <text>
     * server   <server_index>   read            <client_id>     <offset>   <length>
     */
    else if (strcmp(command, "server") == 0)
    {
        // регистрация сервера
        if (arg1 && strcmp(arg1, "register") == 0)
        {
            // если нет имени сервера, выходим
            if (!arg2)
            {
                printf("Usage: server register <name>\n");
                return;
            }
            server_register(arg2);
        }
        // получение id сервера
        else if (parse_index(arg1, &index, MAX_INSTANCES))
        {
            // отображение памяти
            if (arg2 && strcmp(arg2, "mmap") == 0)
            {
                // получение shm_id, для которого надо отобразить общую память
                if (!parse_index(arg3, &index2, MAX_ID_VALUE + 1))
                {
                    printf("Usage: server <server index> mmap <shm_id>\n");
                    return;
                }
                // отображение памяти
                server_mmap(index, index2);
            }
            // отображение информации о сервере
            else if (arg2 && strcmp(arg2, "show") == 0)
            {
                server_show(index);
            }
            // запись в память
            else if (arg2 && strcmp(arg2, "write") == 0)
            {
                char *str_offset = strtok(arg4, " ");
                char *p_end;
                char *text = strtok(NULL, "");

                // получение client_id и проверка: есть ли сдвиг с текстом
                if (parse_index(arg3, &index2, MAX_INSTANCES) && arg4 && str_offset && text)
                {
                    // получение сдвига и текста
                    offset = strtol(str_offset, &p_end, 10);

                    // запись
                    server_write(index, index2, offset, text);
                }
                else
                {
                    printf("Usage: server <server_index> write <client_id> <offset> <text>\n");
                }
            }
            // чтение из памяти
            else if (arg2 && strcmp(arg2, "read") == 0)
            {
                // получение сдвига и текста
                char *str_offset = strtok(arg4, " ");                
                char *p_end;
                char *str_len = strtok(NULL, "");
                
                // получение client_id и проверка: есть ли сдвиг с длинной текста
                if (parse_index(arg3, &index2, MAX_INSTANCES) && arg4 && str_offset && str_len)
                {
                    offset = strtol(str_offset, &p_end, 10);
                    length = strtol(str_len, &p_end, 10);

                    // чтение
                    server_read(index, index2, offset, length);
                }
                else
                {
                    printf("Usage: server <server_index> read <client_id> <offset> <length>\n");
                }
            }
            else
            {

                if (command && arg1 && arg2)
                    printf("Unknown command: %s %s %s\n", command, arg1, arg2);
                else
                    printf("Argument 1 or 2 is empty\n");
            }
        }
        else
        {
            if (command && arg1)
                printf("Unknown command: '%s %s'\n", command, arg1);
            else
                printf("Argument 1 is empty\n");
        }
    }
    /**
     * Печать списка команд
     */
    else if (strcmp(command, "help") == 0)
    {
        print_commands();
    }
    /**
     * Выход из программы. Обрабатывается в main()
     */
    else if (strcmp(command, "exit") == 0)
    {
    }
    /**
     * Неизвестная команда
     */
    else
    {
        printf("Unknown command '%s'\n", command);
    }
}

void cleanup_all()
{
    printf("Cleaning up all instances...\n");
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        if (g_clients[i].active)
        {
            client_cleanup(&g_clients[i]);
        }
        if (g_servers[i].active)
        {
            server_cleanup(&g_servers[i]);
        }
    }
    if (g_dev_fd >= 0)
    {
        close(g_dev_fd);
        g_dev_fd = -1;
        printf("Device closed.\n");
    }
}

// Точка входа
int main()
{
    char input[MAX_INPUT_LEN];
    bool running = true;

    initialize_instances();
    if (!setup_signal_handler() || !open_device())
    {
        return 1;
    }
    get_page_size();

    printf("RIPC Multi-Instance Test Application (PID: %d)\n", getpid());
    // print_commands();

    while (running)
    {
        process_signal(); // Обработка флага сигнала перед запросом ввода

        printf("> ");
        fflush(stdout);

        if (!fgets(input, MAX_INPUT_LEN, stdin))
        {
            if (errno == EINTR)
            { // Прервано сигналом?
                errno = 0;
                clearerr(stdin);
                continue; // Повторить цикл, чтобы обработать флаг сигнала
            }
            break; // EOF или другая ошибка
        }

        // Проверяем на команду exit до вызова strtok
        if (strncmp(input, "exit", 4) == 0 && (input[4] == '\n' || input[4] == '\0' || isspace(input[4])))
        {
            running = false;
            continue;
        }

        process_command(input);
    }

    cleanup_all();
    printf("Application closed\n");
    return 0;
}