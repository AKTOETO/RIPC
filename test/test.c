#include "test.h"
#include <ctype.h> // isspace()
#include <poll.h>  // pollfd
#include <pthread.h>
#include <sys/mman.h> // MAP_FAILED
#include <unistd.h>   // sysconf, getpid, close
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Определения глобальных переменных из test.h (уже объявлены в .h)
// volatile bool g_running = 0;
pthread_t g_listener_tid = 0;
// volatile struct SignalDispatcher g_signal_dispatch_table[MAX_HANDLED_SIGNALS];
// struct ServerInstance g_servers[MAX_INSTANCES];
// struct ClientInstance g_clients[MAX_INSTANCES];
// int g_num_servers = 0;
// int g_num_clients = 0;
// long g_page_size = 4096; // Инициализируется в get_page_size
// int g_dev_fd = -1;

// --- Функции-Обработчики для Конкретных Сигналов ---
void handle_new_connection(const struct notification_data *ntf)
{
    int client_id = ntf->m_sender_id;
    int submem_id = ntf->m_sub_mem_id;
    int server_id = ntf->m_reciver_id;

    // проверка id сервера и клиента
    if (!IS_ID_VALID(server_id) || !IS_ID_VALID(client_id) || !IS_ID_VALID(submem_id))
    {
        printf("Error: Invalid ID in NEW_CONNECTION notification (Srv:%d, Cli:%d, Shm:%d)\n",
               server_id, client_id, submem_id);
        return;
    }

    printf("\n--- [Handler NEW_CONNECTION in Thread %lu] ---\n", (unsigned long)pthread_self());
    printf("  Client ID:         %d\n", client_id);
    printf("  Server ID:         %d\n", server_id);
    printf("  Submem ID:         %d\n", submem_id);
    printf("--------------------------------------------\n");
    printf("> ");
    fflush(stdout);

    // ищем сервер по ID ядра
    struct ServerInstance *srv = find_server_by_driver_id(server_id);
    if (!srv)
    {
        printf("Error: Server instance with driver ID %d not found in this process.\n", server_id);
        return;
    }

    // добавляем информацию о соединении и submemory к найденному серверу
    server_add_connection(srv, client_id, submem_id);
}

void handle_new_message(const struct notification_data *ntf)
{
    printf("\n--- [Handler NEW_MESSAGE in Thread %lu] ---\n", (unsigned long)pthread_self());

    int sender_id = ntf->m_sender_id;
    int receiver_id = ntf->m_reciver_id;
    int sub_mem_id = ntf->m_sub_mem_id; // Важно для сервера

    printf("  Sender Type:     %s (ID: %d)\n", (ntf->m_who_sends == CLIENT) ? "CLIENT" : "SERVER", sender_id);
    printf("  Receiver ID:     %d\n", receiver_id);
    if (ntf->m_who_sends == CLIENT)
    { // Если отправитель - клиент, важен sub_mem_id
        printf("  Target SubMem ID: %d\n", sub_mem_id);
    }
    printf("--------------------------------------------\n");
    printf("> ");
    fflush(stdout);

    // Проверка валидности ID
    if (!IS_ID_VALID(sender_id) || !IS_ID_VALID(receiver_id) || (ntf->m_who_sends == CLIENT && !IS_ID_VALID(sub_mem_id)))
    {
        printf("Error: Invalid ID in NEW_MESSAGE notification (Sender:%d, Recv:%d, SubMem:%d)\n",
               sender_id, receiver_id, sub_mem_id);
        return;
    }

    // разбор по типу отправителя
    switch (ntf->m_who_sends)
    {
    // Отправил клиент -> сообщение для сервера
    case CLIENT:
    {
        // Получатель - это server_id
        struct ServerInstance *srv = find_server_by_driver_id(receiver_id);
        if (!srv)
        {
            printf("Error: cant find server instance with kernel ID:%d\n", receiver_id);
            return;
        }

        // Находим нужную область памяти по sub_mem_id
        struct ServerShmMapping *submem = server_find_submem_by_id(srv, sub_mem_id);
        if (!submem)
        {
            printf("Error: No submem (ID:%d) tracked by server (ID:%d)\n", sub_mem_id, receiver_id);
            // Может быть, соединение было добавлено, но память еще не создана? Проверим.
            int conn_idx = -1;
            for (int i = 0; i < srv->num_connections; ++i)
            {
                if (srv->connections[i].active && srv->connections[i].client_id == sender_id && srv->connections[i].shm_id == sub_mem_id)
                {
                    conn_idx = i;
                    break;
                }
            }
            if (conn_idx != -1)
            {
                printf("Info: Connection found, attempting to create/map submemory %d for server %d\n", sub_mem_id, receiver_id);
                submem = server_create_shm_mapping(srv, sub_mem_id);
                if (!submem)
                {
                    printf("Error: Failed to create mapping slot for submem %d on demand.\n", sub_mem_id);
                    return;
                }
                // Продолжаем к отображению и чтению
            }
            else
            {
                printf("Error: No connection info found matching client %d and shm_id %d for server %d.\n", sender_id, sub_mem_id, receiver_id);
                return;
            }
        }

        // Отображаем память, если она еще не отображена
        if (!submem->mapped)
        {
            printf("Info: Submemory (ID:%d) for server (ID:%d) not mapped yet. Attempting mmap...\n",
                   sub_mem_id, receiver_id);
            if (!server_mmap(srv, sub_mem_id))
            {
                printf("Error: Failed to map submemory (ID:%d) for server (ID:%d) on demand.\n",
                       sub_mem_id, receiver_id);
                return;
            }
            // Убедимся что указатель submem все еще валиден (хотя server_mmap не должен его менять)
            submem = server_find_submem_by_id(srv, sub_mem_id);
            if (!submem || !submem->mapped)
            {
                printf("Error: Failed to re-find mapped submemory %d after mmap attempt.\n", sub_mem_id);
                return;
            }
        }

        // Читаем сообщение
        if (!server_read(srv, submem, 0, SHM_REGION_PAGE_SIZE))
        {
            printf("Error: cant read from server (ID:%d) using submem(ID:%d)\n", receiver_id, sub_mem_id);
            return;
        }
        printf("Msg read in server (ID:%d) from client (ID: %d) via submem (ID:%d)\n", receiver_id, sender_id, sub_mem_id);
    }
    break;

    // Отправил сервер -> сообщение для клиента
    case SERVER:
    {
        // Получатель - это client_id
        // Ищем экземпляр клиента по ID ядра
        struct ClientInstance *cli = NULL;
        for (int i = 0; i < MAX_INSTANCES; ++i)
        {
            if (g_clients[i].active && g_clients[i].client_id == receiver_id)
            {
                cli = &g_clients[i];
                break;
            }
        }

        if (!cli)
        {
            printf("Error: cant find client instance with kernel ID:%d\n", receiver_id);
            return;
        }

        // Отображаем память клиента, если она еще не отображена
        if (!cli->shm.mapped)
        {
            printf("Info: Memory for client (ID:%d) not mapped yet. Attempting mmap...\n", receiver_id);
            if (!client_mmap(cli))
            {
                printf("Error: Failed to map memory for client (ID:%d) on demand.\n", receiver_id);
                return;
            }
        }

        // Читаем сообщение
        if (!client_read(cli, 0, SHM_REGION_PAGE_SIZE))
        {
            printf("Error: cant read msg for client (ID:%d) from server (ID: %d)\n", receiver_id, sender_id);
            return;
        }
        printf("Msg read in client (ID:%d) from server (ID:%d)\n", receiver_id, sender_id);
    }
    break;

    default:
        printf("[handler NEW_MESSAGE] ERROR: Unknown sender type %d\n", ntf->m_who_sends);
    }
}

// Функция поиска и вызова обработчика
void dispatch_signal(const struct notification_data *ntf)
{
    printf("[Dispatcher] Received notification type %d from %s (ID %d) for receiver %d\n",
           ntf->m_type, (ntf->m_who_sends == CLIENT) ? "CLIENT" : "SERVER", ntf->m_sender_id, ntf->m_reciver_id);

    if (!IS_NTF_TYPE_VALID(ntf->m_type))
    {
        printf("[Dispatcher] Warning: Invalid notification type %d\n", ntf->m_type);
        return;
    }
    if (!IS_NTF_SEND_VALID(ntf->m_who_sends))
    {
        printf("[Dispatcher] Warning: Invalid notification sender type %d\n", ntf->m_who_sends);
        return;
    }

    for (int i = 0; g_signal_dispatch_table[i].handler != NULL; ++i)
    {
        // printf("[Dispatcher] Checking table entry %d: type=%d\n", i, g_signal_dispatch_table[i].signo);
        if (g_signal_dispatch_table[i].signo == ntf->m_type)
        {
            printf("[Dispatcher] Found handler for notification type %d. Calling handler...\n", ntf->m_type);
            g_signal_dispatch_table[i].handler(ntf);
            return; // Обработали, выходим
        }
    }
    // Если обработчик не найден
    printf("[Dispatcher] Warning: No handler found for notification type %d\n", ntf->m_type);
    printf("> ");
    fflush(stdout);
}

// --- Поток для Ожидания и Обработки Уведомлений ---
void *signal_listener_thread(void *arg)
{
    (void)arg;
    printf("[Notification Listener]: Thread %lu started.\n", (unsigned long)pthread_self());

    struct pollfd pfd;

    // Проверяем валидность g_dev_fd перед использованием
    if (g_dev_fd < 0)
    {
        fprintf(stderr, "[Notification Listener] Error: Invalid device fd (%d). Thread exiting.\n", g_dev_fd);
        return NULL;
    }

    pfd.fd = g_dev_fd;
    pfd.events = POLLIN; // Ждем только событие чтения (уведомления от драйвера)
    pfd.revents = 0;     // Инициализация

    printf("[Notification Listener] Waiting for notifications on fd %d\n", g_dev_fd);

    while (g_running)
    {
        int ret = poll(&pfd, 1, 1000); // Таймаут 1 сек для проверки g_running

        if (!g_running)
        {
            printf("[Notification Listener]: Stopping thread\n");
            break;
        }

        if (ret == -1)
        {
            if (errno == EINTR)
            {
                // printf("[Notification Listener] poll interrupted by EINTR. Continuing...\n");
                continue;
            }
            perror("[Notification Listener] poll failed");
            g_running = false; // Сигнализируем основному потоку об ошибке
            break;
        }
        else if (ret > 0) // Есть событие
        {
            // printf("[Notification Listener] poll returned > 0, revents: 0x%x\n", pfd.revents);

            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                fprintf(stderr, "[Notification Listener] Error event on device fd (revents: 0x%x). Exiting loop.\n", pfd.revents);
                g_running = false; // Сигнализируем об ошибке
                break;
            }

            if (pfd.revents & POLLIN)
            {
                // printf("[Notification listener] Got POLLIN event - potential new notification(s)\n");

                struct notification_data ntf;
                ssize_t s;

                // Читаем все доступные уведомления в неблокирующем режиме
                while (g_running)
                {
                    s = read(g_dev_fd, &ntf, sizeof(ntf));

                    if (s == sizeof(ntf))
                    {
                        // Успешно прочитали уведомление
                        // printf("[Notification Listener] Successfully read notification (type %d)\n", ntf.m_type);
                        dispatch_signal(&ntf); // Вызываем диспетчер
                    }
                    else if (s == 0)
                    {
                        // Это не ожидается для символьного устройства, но обработаем
                        fprintf(stderr, "[Notification Listener] read returned 0 (EOF?). Exiting loop.\n");
                        g_running = false;
                        break; // Выходим из цикла чтения
                    }
                    else if (s < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            // Данных больше нет, это нормально для неблокирующего режима
                            // printf("[Notification Listener] read EAGAIN/EWOULDBLOCK - finished reading pending notifications.\n");
                            break; // Выходим из цикла чтения (read loop)
                        }
                        else if (errno == EINTR)
                        {
                            // printf("[Notification Listener] read interrupted by EINTR. Continuing read loop...\n");
                            continue; // Повторяем чтение
                        }
                        else
                        {
                            perror("[Notification Listener] read failed");
                            //g_running = false;
                            break;             // Выходим из цикла чтения
                        }
                    }
                    else // s > 0 && s != sizeof(ntf)
                    {
                        // Прочитали неполное уведомление? Это ошибка.
                        fprintf(stderr, "[Notification Listener] Unexpected read size %zd (expected %zu). Flushing possible remaining data.\n",
                                s, sizeof(ntf));
                        // Попытка очистить буфер чтения (просто чтение до EAGAIN)
                        //char dummy_buf[256];
                        //while (read(g_dev_fd, dummy_buf, sizeof(dummy_buf)) > 0 || errno == EINTR);
                        break; // Выходим из цикла чтения после ошибки
                    }
                } // end read loop
            }
            // else if (pfd.revents != 0) // Обработка других событий уже выше (POLLERR и т.д.)
            //{
            //    printf("[Notification Listener] Unexpected poll event: 0x%x\n", pfd.revents);
            //}
        }
        // else: ret == 0 (таймаут), просто продолжаем цикл poll
    } // end while(g_running)

    printf("[Notification Listener Thread %lu] Exiting.\n", (unsigned long)pthread_self());
    return NULL;
}

// Инициализация
void initialize_instances()
{
    // Таблица диспетчера уведомлений
    g_signal_dispatch_table[0].signo = NEW_CONNECTION;
    g_signal_dispatch_table[0].handler = handle_new_connection;
    g_signal_dispatch_table[1].signo = NEW_MESSAGE;
    g_signal_dispatch_table[1].handler = handle_new_message;
    // ... добавить другие типы уведомлений ...
    g_signal_dispatch_table[2].signo = 0; // Терминатор таблицы
    g_signal_dispatch_table[2].handler = NULL;

    // Инициализация массивов клиентов и серверов
    memset(g_servers, 0, sizeof(g_servers));
    memset(g_clients, 0, sizeof(g_clients));
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        g_servers[i].server_id = -1;
        g_servers[i].active = false;
        g_clients[i].client_id = -1;
        g_clients[i].active = false;
        g_clients[i].shm.addr = MAP_FAILED;
        g_clients[i].shm.mapped = false;

        for (int j = 0; j < MAX_SERVER_CLIENTS; ++j)
            g_servers[i].connections[j].active = false;
        for (int j = 0; j < MAX_SERVER_SHM; ++j)
        {
            g_servers[i].mappings[j].mapped = false;
            g_servers[i].mappings[j].shm_id = -1;
            g_servers[i].mappings[j].addr = MAP_FAILED;
        }
        g_servers[i].num_connections = 0;
        g_servers[i].num_mappings = 0;
    }
    g_num_servers = 0;
    g_num_clients = 0;
    g_running = 1; // Устанавливаем флаг работы
}

bool setup_signal_handler() // Название не совсем точное, т.к. работаем с poll/read
{
    printf("Initializing notification listener thread...\n");

    // Запускаем поток-слушатель
    if (pthread_create(&g_listener_tid, NULL, signal_listener_thread, NULL) != 0)
    {
        perror("Error creating notification listener thread");
        return false;
    }
    printf("Notification listener thread started (TID: %lu).\n", (unsigned long)g_listener_tid);
    return true;
}

bool open_device()
{
    // Открываем в неблокирующем режиме для read, чтобы poll/read работали корректно
    g_dev_fd = open(DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (g_dev_fd < 0)
    {
        perror(DEVICE_PATH);
        fprintf(stderr, "Make sure the '%s' module is loaded and you have permissions.\n", DEVICE_NAME);
        return false;
    }
    printf("Device '%s' opened successfully (fd=%d)\n", DEVICE_PATH, g_dev_fd);
    return true;
}

void get_page_size()
{
    g_page_size = sysconf(_SC_PAGE_SIZE);
    if (g_page_size <= 0) // Проверка на <= 0
    {
        perror("sysconf(_SC_PAGE_SIZE) failed");
        // PAGE_SIZE из ripc.h может быть не определен здесь, используем константу
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
    if (!arg)
        return false;
    char *endptr;
    errno = 0; // Сброс errno перед вызовом strtol
    long val = strtol(arg, &endptr, 10);

    // Проверка ошибок strtol и полного разбора строки
    if (errno != 0 || endptr == arg || *endptr != '\0')
    {
        return false;
    }
    // Проверка диапазона
    if (val < 0 || val >= max_val)
    {
        return false;
    }
    *index = (int)val;
    return true;
}

// --- Функции Поиска ---

// Ищет сервер в g_servers по ID, полученному от драйвера
struct ServerInstance *find_server_by_driver_id(int id)
{
    if (!IS_ID_VALID(id))
    {
        // Не печатаем ошибку здесь, вызывающий код может обработать NULL
        // printf("Error: driver_id=%d is not valid for searching server.\n", id);
        return NULL;
    }
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        // Ищем активный сервер с совпадающим ID ядра
        if (g_servers[i].active && g_servers[i].server_id == id)
        {
            return &g_servers[i];
        }
    }
    return NULL; // Не найден
}

// Ищет сервер по индексу в массиве g_servers (для команд пользователя)
struct ServerInstance *find_server_instance(int index)
{
    if (index < 0 || index >= MAX_INSTANCES)
    {
        // Не печатаем здесь, вызывающий код обработает NULL
        // printf("Error: Server index %d out of bounds [0-%d].\n", index, MAX_INSTANCES - 1);
        return NULL;
    }
    if (!g_servers[index].active)
    {
        // Неактивный слот - не ошибка поиска, просто его нет
        return NULL;
    }
    return &g_servers[index];
}

// Ищет клиента по индексу в массиве g_clients (для команд пользователя)
struct ClientInstance *find_client_instance(int index)
{
    if (index < 0 || index >= MAX_INSTANCES)
    {
        // printf("Error: Client index %d out of bounds [0-%d].\n", index, MAX_INSTANCES - 1);
        return NULL;
    }
    if (!g_clients[index].active)
    {
        return NULL;
    }
    return &g_clients[index];
}

// --- Вспомогательные функции для Сервера ---

// Ищет индекс слота маппинга по shm_id у сервера
int server_find_shm_mapping_index(struct ServerInstance *server, int shm_id)
{
    if (!server || !IS_ID_VALID(shm_id))
        return -1;
    for (int i = 0; i < server->num_mappings; ++i)
    {
        // Сравниваем ID в уже занятых слотах
        if (server->mappings[i].shm_id == shm_id)
        {
            return i;
        }
    }
    return -1; // Не найдено
}

// Ищет указатель на структуру маппинга по shm_id
struct ServerShmMapping *server_find_submem_by_id(struct ServerInstance *server, int sub_mem_id)
{
    if (!server || !IS_ID_VALID(sub_mem_id))
        return NULL;
    int index = server_find_shm_mapping_index(server, sub_mem_id);
    if (index != -1)
    {
        // Возвращаем указатель на найденный элемент
        return &server->mappings[index];
    }
    return NULL; // Не найдено
}

// Создает запись для отслеживания shm_id (не сам mmap!)
struct ServerShmMapping *server_create_shm_mapping(struct ServerInstance *server, int sub_mem_id)
{
    if (!server || !IS_ID_VALID(sub_mem_id))
        return NULL;

    // Проверяем, нет ли уже такого ID
    if (server_find_shm_mapping_index(server, sub_mem_id) != -1)
    {
        // Уже существует, не создаем новый
        // printf("Server %d: SHM mapping slot for sub_mem_id %d already exists.\n", server->server_id, sub_mem_id);
        return server_find_submem_by_id(server, sub_mem_id); // Возвращаем существующий
    }

    // Ищем свободный слот в массиве mappings
    if (server->num_mappings < MAX_SERVER_SHM)
    {
        int new_index = server->num_mappings; // Индекс нового элемента
        // Инициализируем новый слот
        server->mappings[new_index].shm_id = sub_mem_id;
        server->mappings[new_index].mapped = false;
        server->mappings[new_index].addr = MAP_FAILED;
        server->mappings[new_index].size = 0;
        server->num_mappings++; // Увеличиваем счетчик используемых слотов
        printf("Server %d: Created tracking slot for shm_id %d at index %d.\n",
               server->server_id, sub_mem_id, new_index);
        return &server->mappings[new_index]; // Возвращаем указатель на новый слот
    }
    else
    {
        // Нет свободных слотов
        printf("Server %d: Cannot create new SHM mapping slot (limit %d reached).\n",
               server->server_id, MAX_SERVER_SHM);
        return NULL;
    }
}

// Ищет индекс активного соединения по client_id у сервера
int server_find_connection_index(struct ServerInstance *server, int client_id)
{
    if (!server || !IS_ID_VALID(client_id))
        return -1;
    for (int i = 0; i < server->num_connections; ++i)
    {
        // Ищем только среди активных соединений
        if (server->connections[i].active && server->connections[i].client_id == client_id)
        {
            return i;
        }
    }
    return -1; // Не найдено
}

// Добавляет или активирует информацию о соединении у сервера
void server_add_connection(struct ServerInstance *server, int client_id, int shm_id)
{
    if (!server || !IS_ID_VALID(client_id) || !IS_ID_VALID(shm_id))
    {
        printf("Error: server_add_connection invalid arguments.\n");
        return;
    }

    // Проверка на уже существующее активное соединение с этим client_id
    int existing_index = server_find_connection_index(server, client_id);
    if (existing_index != -1)
    {
        // Соединение уже активно
        if (server->connections[existing_index].shm_id == shm_id)
        {
            // Точно такое же соединение, ничего не делаем
            printf("Server %d: Connection info (client %d -> shm %d) already exists and is active at index %d.\n",
                   server->server_id, client_id, shm_id, existing_index);
            return;
        }
        else
        {
            // Тот же клиент, но другой shm_id? Это странно, но обработаем как обновление
            printf("Warning: Server %d: Client %d re-connected with different shm_id (old %d -> new %d) at index %d. Updating.\n",
                   server->server_id, client_id, server->connections[existing_index].shm_id, shm_id, existing_index);
            // Обновляем shm_id для существующего соединения
            server->connections[existing_index].shm_id = shm_id;
            // Убедимся, что слот для нового shm_id существует (или создаем его)
            struct ServerShmMapping *mapping = server_create_shm_mapping(server, shm_id);
            if (!mapping)
            {
                printf("Error: Server %d: Failed to create/find mapping for updated shm_id %d.\n", server->server_id, shm_id);
                // Откатить изменение shm_id? Пока оставим так.
            }
            else
            {
                // Попытаемся сразу отобразить новую память
                if (!mapping->mapped)
                {
                    server_mmap(server, shm_id);
                }
            }
            return; // Выходим после обновления
        }
    }

    // Ищем неактивный слот для переиспользования
    int index = -1;
    for (int i = 0; i < server->num_connections; ++i)
    {
        if (!server->connections[i].active)
        {
            index = i;
            printf("Server %d: Reusing inactive connection slot %d for client %d -> shm %d.\n",
                   server->server_id, index, client_id, shm_id);
            break;
        }
    }

    // Если не нашли неактивный и есть место, берем новый слот
    if (index == -1 && server->num_connections < MAX_SERVER_CLIENTS)
    {
        index = server->num_connections++; // Берем следующий свободный слот и увеличиваем счетчик
        printf("Server %d: Using new connection slot %d for client %d -> shm %d.\n",
               server->server_id, index, client_id, shm_id);
    }
    else if (index == -1)
    {
        // Мест нет
        printf("Server %d: Cannot add connection for client %d (limit %d reached).\n",
               server->server_id, client_id, MAX_SERVER_CLIENTS);
        return;
    }

    // Нашли/выделили слот 'index'

    // Убеждаемся, что слот для shm_id существует (или создаем его)
    struct ServerShmMapping *mapping = server_create_shm_mapping(server, shm_id);
    if (!mapping)
    {
        printf("Error: Server %d: Cannot create/find SHM mapping slot for shm_id %d.\n", server->server_id, shm_id);
        // Если это был новый слот соединения, откатываем счетчик?
        if (index == server->num_connections - 1)
            server->num_connections--;
        return;
    }

    // Заполняем и активируем слот соединения
    server->connections[index].client_id = client_id;
    server->connections[index].shm_id = shm_id;
    server->connections[index].active = true;

    // Попытка отобразить память сразу при добавлении соединения
    if (!mapping->mapped)
    {
        printf("Server %d: Attempting mmap for shm_id %d upon connection.\n", server->server_id, shm_id);
        server_mmap(server, shm_id);
    }

    printf("Server %d: Connection slot %d activated for client %d -> shm %d.\n", server->server_id, index, client_id, shm_id);
}

// --- Основные Действия с Сервером ---

// Регистрация сервера
bool server_register(const char *name)
{
    if (!name || strlen(name) == 0)
    {
        printf("Error: Server name cannot be empty.\n");
        return false;
    }
    if (strlen(name) >= MAX_SERVER_NAME)
    {
        printf("Error: Server name is too long (max %d chars).\n", MAX_SERVER_NAME - 1);
        return false;
    }

    // Ищем свободный слот в массиве g_servers
    int free_index = -1;
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (!g_servers[i].active)
        {
            free_index = i;
            break;
        }
    }

    if (free_index == -1)
    {
        printf("Error: No free slots for a new server instance (max %d reached).\n", MAX_INSTANCES);
        return false;
    }

    // Подготовка данных для IOCTL
    struct server_registration reg_ioctl_data;
    strncpy(reg_ioctl_data.name, name, MAX_SERVER_NAME - 1);
    reg_ioctl_data.name[MAX_SERVER_NAME - 1] = '\0';
    reg_ioctl_data.server_id = -1; // Ядро должно присвоить ID

    // Вызов IOCTL
    if (ioctl(g_dev_fd, IOCTL_REGISTER_SERVER, &reg_ioctl_data) < 0)
    {
        perror("IOCTL_REGISTER_SERVER failed");
        return false;
    }

    // Проверка ID, возвращенного ядром
    if (!IS_ID_VALID(reg_ioctl_data.server_id))
    {
        printf("Error: Kernel returned invalid server_id %d after registration.\n", reg_ioctl_data.server_id);
        // Возможно, стоит попытаться отменить регистрацию, если есть такой ioctl
        return false;
    }

    // Успех: Инициализируем найденный свободный слот
    struct ServerInstance *srv = &g_servers[free_index];
    // Очищаем слот перед использованием на всякий случай
    memset(srv, 0, sizeof(struct ServerInstance));
    srv->server_id = reg_ioctl_data.server_id;
    strncpy(srv->name, reg_ioctl_data.name, MAX_SERVER_NAME - 1);
    srv->name[MAX_SERVER_NAME - 1] = '\0'; // Гарантируем null-termination
    srv->active = true;
    srv->num_connections = 0;
    srv->num_mappings = 0;
    // Явная инициализация вложенных массивов (хотя memset уже сделал это)
    for (int j = 0; j < MAX_SERVER_CLIENTS; ++j)
        srv->connections[j].active = false;
    for (int j = 0; j < MAX_SERVER_SHM; ++j)
    {
        srv->mappings[j].mapped = false;
        srv->mappings[j].shm_id = -1;
        srv->mappings[j].addr = MAP_FAILED;
        srv->mappings[j].size = 0;
    }

    g_num_servers++; // Увеличиваем глобальный счетчик
    printf("Server instance %d registered as '%s' with driver_ID: %d\n", free_index, srv->name, srv->server_id);
    return true;
}

// Отображение памяти для конкретного shm_id у сервера
bool server_mmap(struct ServerInstance *server, int shm_id)
{
    // Проверки аргументов
    if (!server)
    {
        printf("Error: server_mmap called with NULL server pointer.\n");
        return false;
    }
    if (!server->active)
    {
        printf("Error: server_mmap called for inactive server instance (ID: %d).\n", server->server_id);
        return false;
    }
    if (!IS_ID_VALID(shm_id))
    {
        printf("Error: server_mmap called with invalid shm_id: %d\n", shm_id);
        return false;
    }

    // Находим или создаем слот для отслеживания этого shm_id
    struct ServerShmMapping *mapping = server_create_shm_mapping(server, shm_id);
    if (!mapping)
    {
        // server_create_shm_mapping уже вывел ошибку
        return false;
    }

    // Проверяем, не отображена ли уже память для этого слота
    if (mapping->mapped)
    {
        printf("Server %d: Memory for shm_id %d already mapped at %p\n", server->server_id, shm_id, mapping->addr);
        return true; // Уже сделано
    }

    // Формируем packed_id для mmap (ID сервера, ID submemory)
    u32 packed_id = pack_ids(server->server_id, shm_id);
    if (packed_id == (u32)-EINVAL)
    {
        // pack_ids уже вывел ошибку
        return false;
    }

    // Вычисляем смещение для mmap
    off_t offset_for_mmap = (off_t)packed_id * g_page_size;

    printf("Server %d: Attempting mmap for shm_id %d with offset 0x%lx (packed 0x%x, page_size %ld)\n",
           server->server_id, shm_id, (unsigned long)offset_for_mmap, packed_id, g_page_size);

    // Выполняем mmap
    void *mapped_addr = mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, g_dev_fd, offset_for_mmap);

    if (mapped_addr == MAP_FAILED)
    {
        perror("Server mmap failed");
        // Не меняем состояние mapping, оно все еще {mapped=false, addr=MAP_FAILED}
        return false;
    }
    else
    {
        // Успех: обновляем структуру ServerShmMapping
        mapping->addr = mapped_addr;
        mapping->size = SHM_REGION_PAGE_SIZE;
        mapping->mapped = true;
        printf("Server %d: Memory for shm_id %d mapped successfully at: %p\n", server->server_id, shm_id, mapping->addr);
        return true;
    }
}

// Запись данных от сервера клиенту
bool server_write(struct ServerInstance *server, int client_id, long offset, const char *text)
{
    // Проверка входных данных
    if (!server || !server->active || !text || !IS_ID_VALID(client_id))
    {
        printf("Error: server_write invalid arguments (server=%p, active=%d, text=%p, client_id=%d)\n",
               (void *)server, server ? server->active : 0, (void *)text, client_id);
        return false;
    }

    // Ищем активное соединение с нужным client_id
    int conn_index = server_find_connection_index(server, client_id);
    if (conn_index == -1)
    {
        printf("Server %d: Active connection to client ID %d not found.\n", server->server_id, client_id);
        return false;
    }
    // Получаем shm_id для этого соединения
    int target_shm_id = server->connections[conn_index].shm_id;

    // Ищем соответствующий маппинг памяти
    struct ServerShmMapping *mapping = server_find_submem_by_id(server, target_shm_id);
    if (!mapping)
    {
        printf("Internal Error: Server %d: Mapping for SHM ID %d (client %d) not found, though connection exists.\n", server->server_id, target_shm_id, client_id);
        // Попробовать создать? Это не должно происходить, если add_connection работает.
        return false;
    }
    if (!mapping->mapped)
    {
        printf("Server %d: Memory for SHM ID %d (client %d) is not mapped. Cannot write.\n", server->server_id, target_shm_id, client_id);
        // Попытаться отобразить?
        printf("Attempting to mmap shm_id %d on demand for write...\n", target_shm_id);
        if (!server_mmap(server, target_shm_id))
        {
            printf("Failed to mmap shm_id %d on demand.\n", target_shm_id);
            return false;
        }
        // Повторно получаем указатель на случай, если create_mapping вернул новый
        mapping = server_find_submem_by_id(server, target_shm_id);
        if (!mapping || !mapping->mapped)
        { // Проверка после попытки mmap
            printf("Error: Failed to find mapped memory %d even after mmap attempt.\n", target_shm_id);
            return false;
        }
    }

    // Проверка смещения и размера
    if (offset < 0 || (size_t)offset >= mapping->size)
    {
        printf("Error: Invalid offset %ld for server write (max %zu)\n", offset, mapping->size - 1);
        return false;
    }
    size_t text_len = strlen(text);
    size_t available_space = mapping->size - (size_t)offset;
    size_t write_len = (text_len < available_space) ? text_len : available_space;

    // Копируем данные
    memcpy((char *)mapping->addr + offset, text, write_len);

    // Логируем запись
    if (write_len == text_len && write_len < available_space)
    {
        ((char *)mapping->addr)[offset + write_len] = '\0';
        printf("Server %d wrote %zu bytes (null term) to shm_id %d (for client %d) at offset %ld: \"%s\"\n",
               server->server_id, write_len + 1, target_shm_id, client_id, offset, text);
    }
    else
    {
        printf("Server %d wrote %s%zu bytes to shm_id %d (for client %d) at offset %ld: \"%.*s%s\"\n",
               server->server_id,
               (write_len < text_len) ? "truncated " : "",
               write_len, target_shm_id, client_id, offset,
               (int)write_len, text, (write_len < text_len) ? "..." : "");
    }

    // Уведомляем драйвер об окончании записи сервером
    u32 packed_server_shm_id = pack_ids(server->server_id, target_shm_id);
    if (packed_server_shm_id == (u32)-EINVAL)
    {
        printf("Error: Failed to pack server/shm ID for IOCTL_SERVER_END_WRITING.\n");
        // Продолжаем выполнение, но без уведомления? Или вернуть ошибку? Пока продолжаем.
    }
    else
    {
        printf("Server %d: Sending IOCTL_SERVER_END_WRITING (packed_id 0x%x) for client %d\n",
               server->server_id, packed_server_shm_id, client_id);
        if (ioctl(g_dev_fd, IOCTL_SERVER_END_WRITING, packed_server_shm_id) < 0)
        {
            perror("IOCTL_SERVER_END_WRITING failed");
            // Не фатально для самой записи, но уведомление не будет отправлено
        }
    }

    return true;
}
// Чтение данных сервером из памяти, связанной с submem
bool server_read(struct ServerInstance *server, struct ServerShmMapping *submem, long offset, long length)
{
    // Проверка входных данных
    if (!server || !submem || !server->active || !IS_ID_VALID(submem->shm_id))
    {
        printf("Error: server_read invalid arguments (server=%p, submem=%p, active=%d, shm_id=%d)\n",
               (void *)server, (void *)submem, server ? server->active : 0, submem ? submem->shm_id : -1);
        return false;
    }

    if (!submem->mapped)
    {
        printf("Server %d: Memory for SHM ID %d is not mapped. Cannot read.\n", server->server_id, submem->shm_id);
        // Попытаться отобразить?
        printf("Attempting to mmap shm_id %d on demand for read...\n", submem->shm_id);
        if (!server_mmap(server, submem->shm_id))
        {
            printf("Failed to mmap shm_id %d on demand.\n", submem->shm_id);
            return false;
        }
        // Указатель submem должен остаться валидным, т.к. mmap не меняет массив mappings
        if (!submem->mapped)
        { // Проверка после попытки mmap
            printf("Error: Failed to map memory %d even after mmap attempt.\n", submem->shm_id);
            return false;
        }
    }

    // Проверка смещения и длины
    if (offset < 0 || length <= 0 || (size_t)offset >= submem->size)
    {
        printf("Error: Invalid offset (%ld) or length (%ld) for server read (max size %zu)\n", offset, length, submem->size);
        return false;
    }

    // Корректировка длины чтения
    size_t read_length = (size_t)length;
    if ((size_t)offset + read_length > submem->size)
    {
        read_length = submem->size - (size_t)offset;
        printf("Warning: Server read length truncated from %ld to %zu bytes to fit buffer.\n", length, read_length);
    }

    // Выделяем буфер для чтения + нуль-терминатор
    char *read_buf = malloc(read_length + 1);
    if (!read_buf)
    {
        perror("malloc failed for server read buffer");
        return false;
    }

    // Копируем данные и добавляем нуль-терминатор
    memcpy(read_buf, (char *)submem->addr + offset, read_length);
    read_buf[read_length] = '\0';

    printf("Server %d read from shm_id %d at offset %ld (%zu bytes): \"%s\"\n",
           server->server_id, submem->shm_id, offset, read_length, read_buf);
    free(read_buf);
    return true;
}
// Отображение информации о сервере
void server_show(struct ServerInstance *server)
{
    if (!server)
    {
        printf("Error: server_show called with NULL server pointer.\n");
        return;
    }

    // Найдем индекс в массиве g_servers для user-friendly вывода
    int instance_index = -1;
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        if (&g_servers[i] == server)
        {
            instance_index = i;
            break;
        }
    }

    printf("--- Server Instance %d State ---\n", instance_index);
    if (!server->active)
    {
        printf("  Status:        Inactive\n");
        printf("-------------------------------\n");
        return;
    }
    printf("  Status:        Active\n");
    printf("  Name:          '%s'\n", server->name);
    printf("  Registered ID: %d\n", server->server_id);
    printf("  Process PID:   %d\n", getpid());

    printf("  Connections (%d slots used / %d max):\n", server->num_connections, MAX_SERVER_CLIENTS);
    int active_conn_count = 0;
    for (int i = 0; i < server->num_connections; ++i)
    {
        printf("    Slot %d: Client ID: %-5d -> SHM ID: %-5d (Active: %s)\n",
               i,
               server->connections[i].client_id, // Отображаем даже для неактивных слотов, если они были использованы
               server->connections[i].shm_id,
               server->connections[i].active ? "Yes" : "No ");
        if (server->connections[i].active)
            active_conn_count++;
    }
    if (server->num_connections == 0)
        printf("    (No connection slots ever used)\n");
    printf("    (Total currently active connections: %d)\n", active_conn_count);

    printf("  SHM Mappings (%d slots used / %d max):\n", server->num_mappings, MAX_SERVER_SHM);
    int active_map_count = 0;
    for (int i = 0; i < server->num_mappings; ++i)
    {
        printf("    Slot %d: SHM ID: %-5d -> Addr: %-14p Size: %-6zu Mapped: %s\n",
               i,
               server->mappings[i].shm_id, // Отображаем все использованные слоты
               server->mappings[i].mapped ? server->mappings[i].addr : (void *)-1L,
               server->mappings[i].mapped ? server->mappings[i].size : 0,
               server->mappings[i].mapped ? "Yes" : "No ");
        if (server->mappings[i].mapped)
            active_map_count++;
    }
    if (server->num_mappings == 0)
        printf("    (No SHM mapping slots ever used)\n");
    printf("    (Total currently mapped regions: %d)\n", active_map_count);
    printf("-------------------------------\n");
}

// Очистка сервера
void server_cleanup(struct ServerInstance *server)
{
    if (!server || !server->active)
        return; // Ничего не делаем, если NULL или уже неактивен

    printf("Cleaning up server instance '%s' (ID %d)...\n", server->name, server->server_id);
    //int server_id_copy = server->server_id; // Копируем ID на случай, если он нужен для unregister
    bool was_active = server->active;       // Запоминаем статус

    // 1. Unmap всех отображенных областей памяти
    int unmapped_count = 0;
    for (int i = 0; i < server->num_mappings; ++i)
    {
        // Проверяем именно флаг mapped
        if (server->mappings[i].mapped && server->mappings[i].addr != MAP_FAILED)
        {
            printf("  Unmapping shm_id %d at %p (size %zu)\n",
                   server->mappings[i].shm_id, server->mappings[i].addr, server->mappings[i].size);
            if (munmap(server->mappings[i].addr, server->mappings[i].size) == 0)
            {
                unmapped_count++;
            }
            else
            {
                perror("  munmap failed during cleanup");
                // Продолжаем очистку других ресурсов
            }
            // Сбрасываем состояние маппинга в любом случае
            server->mappings[i].mapped = false;
            server->mappings[i].addr = MAP_FAILED;
            server->mappings[i].size = 0;
            // shm_id оставляем для истории/отладки
        }
    }
    if (unmapped_count > 0)
        printf("  Successfully unmapped %d regions.\n", unmapped_count);

    // 2. TODO: Отправить IOCTL_UNREGISTER_SERVER, если он реализован в ядре
    // if (ioctl(g_dev_fd, IOCTL_UNREGISTER_SERVER, &server_id_copy) < 0) {
    //     // Не фатально, если ioctl нет или произошла ошибка
    //     // perror("  IOCTL_UNREGISTER_SERVER failed (or not implemented)");
    // } else {
    //     printf("  Server ID %d unregistered request sent to kernel.\n", server_id_copy);
    // }

    // 3. Помечаем слот как неактивный в нашем приложении
    server->active = false;
    server->server_id = -1; // Сбрасываем ID
    // Обнулять ли имя и счетчики? Для чистоты - да.
    memset(server->name, 0, MAX_SERVER_NAME);
    server->num_connections = 0; // Сбрасываем счетчики
    server->num_mappings = 0;
    // Обнуляем массивы connections и mappings
    memset(server->connections, 0, sizeof(server->connections));
    memset(server->mappings, 0, sizeof(server->mappings));
    // Инициализируем значения по умолчанию
    for (int j = 0; j < MAX_SERVER_SHM; ++j)
    {
        server->mappings[j].shm_id = -1;
        server->mappings[j].addr = MAP_FAILED;
    }

    // 4. Уменьшаем глобальный счетчик, только если экземпляр был активен
    if (was_active)
    {
        g_num_servers--;
    }
    printf("Server instance cleanup complete.\n");
}

// --- Основные Действия с Клиентом ---

// Регистрация клиента
bool client_register()
{
    // Ищем свободный слот
    int free_index = -1;
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (!g_clients[i].active)
        {
            free_index = i;
            break;
        }
    }

    if (free_index == -1)
    {
        printf("Error: No free slots for a new client instance (max %d reached).\n", MAX_INSTANCES);
        return false;
    }

    // Переменная для получения ID от ядра (должна быть int, как указано в IOCTL)
    int temp_client_id = -1;
    // Вызов IOCTL_REGISTER_CLIENT
    if (ioctl(g_dev_fd, IOCTL_REGISTER_CLIENT, &temp_client_id) < 0)
    {
        perror("IOCTL_REGISTER_CLIENT failed");
        return false;
    }

    // Проверка ID, возвращенного ядром
    if (!IS_ID_VALID(temp_client_id))
    {
        printf("Error: Kernel returned invalid client_id %d after registration.\n", temp_client_id);
        // Попытаться отменить регистрацию?
        return false;
    }

    // Успех: Инициализируем найденный свободный слот
    struct ClientInstance *clnt = &g_clients[free_index];
    memset(clnt, 0, sizeof(struct ClientInstance)); // Очищаем слот
    clnt->client_id = temp_client_id;
    clnt->active = true;
    clnt->shm.mapped = false;
    clnt->shm.addr = MAP_FAILED;
    clnt->shm.size = 0;
    clnt->connected_server_name[0] = '\0';
    clnt->connected_server_id = -1; // ID сервера пока неизвестен

    g_num_clients++; // Увеличиваем глобальный счетчик
    printf("Client instance %d registered with driver ID: %d\n", free_index, clnt->client_id);
    return true;
}

// Подключение клиента к серверу
bool client_connect(struct ClientInstance *client, const char *server_name)
{
    // Проверка входных данных
    if (!client || !client->active || !server_name || strlen(server_name) == 0)
    {
        printf("Error: client_connect invalid arguments (client=%p, active=%d, name='%s')\n",
               (void *)client, client ? client->active : 0, server_name ? server_name : "NULL");
        return false;
    }
    if (strlen(server_name) >= MAX_SERVER_NAME)
    {
        printf("Error: Server name '%s' is too long (max %d chars).\n", server_name, MAX_SERVER_NAME - 1);
        return false;
    }

    // Заполняем структуру для IOCTL (согласно ripc.h)
    struct connect_to_server con_ioctl_data;
    con_ioctl_data.client_id = client->client_id; // ID клиента, который вызывает ioctl
    strncpy(con_ioctl_data.server_name, server_name, MAX_SERVER_NAME - 1);
    con_ioctl_data.server_name[MAX_SERVER_NAME - 1] = '\0';

    printf("Client %d: Attempting to connect to server '%s'...\n", client->client_id, server_name);
    // Отправляем запрос на подключение
    if (ioctl(g_dev_fd, IOCTL_CONNECT_TO_SERVER, &con_ioctl_data) < 0)
    {
        perror("IOCTL_CONNECT_TO_SERVER failed");
        // Сбрасываем имя сервера и ID при ошибке
        client->connected_server_name[0] = '\0';
        client->connected_server_id = -1;
        return false;
    }

    // Успех IOCTL: ядро должно было отправить уведомление NEW_CONNECTION серверу.

    // Сохраняем имя сервера (ID сервера мы не знаем из этого вызова)
    strncpy(client->connected_server_name, server_name, MAX_SERVER_NAME - 1);
    client->connected_server_name[MAX_SERVER_NAME - 1] = '\0';

    // Найдем индекс клиента в массиве для логгирования
    int instance_index = -1;
    for (int i = 0; i < MAX_INSTANCES; ++i)
        if (&g_clients[i] == client)
        {
            instance_index = i;
            break;
        }

    printf("Client %d (instance %d): Connect request sent for server '%s'.\n",
           client->client_id, instance_index, server_name);
    printf("Client %d should now perform 'client %d mmap'. Server '%s' should receive NEW_CONNECTION notification.\n",
           client->client_id, instance_index, server_name);

    return true;
}

// Отображение памяти для клиента
bool client_mmap(struct ClientInstance *client)
{
    // Проверка входных данных
    if (!client || !client->active)
    {
        printf("Error: client_mmap invalid arguments (client=%p, active=%d)\n",
               (void *)client, client ? client->active : 0);
        return false;
    }

    // Проверяем, не отображена ли уже память
    if (client->shm.mapped)
    {
        printf("Client %d: Memory already mapped at %p\n", client->client_id, client->shm.addr);
        return true; // Уже сделано
    }

    // Формируем packed_id для mmap (ID клиента, 0)
    u32 packed_id = pack_ids(client->client_id, 0);
    if (packed_id == (u32)-EINVAL)
    {
        // pack_ids уже вывел ошибку
        return false;
    }

    // Вычисляем смещение для mmap
    off_t offset_for_mmap = (off_t)packed_id * g_page_size;

    // Найдем индекс клиента в массиве для логгирования
    int instance_index = -1;
    for (int i = 0; i < MAX_INSTANCES; ++i)
        if (&g_clients[i] == client)
        {
            instance_index = i;
            break;
        }

    printf("Client %d (instance %d): Attempting mmap with offset 0x%lx (packed 0x%x, page_size %ld)\n",
           client->client_id, instance_index, (unsigned long)offset_for_mmap, packed_id, g_page_size);

    // Выполняем mmap
    void *mapped_addr = mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, g_dev_fd, offset_for_mmap);

    if (mapped_addr == MAP_FAILED)
    {
        perror("Client mmap failed");
        // Сбрасываем состояние на всякий случай
        client->shm.mapped = false;
        client->shm.addr = MAP_FAILED;
        client->shm.size = 0;
        return false;
    }
    else
    {
        // Успех: обновляем структуру клиента
        client->shm.addr = mapped_addr;
        client->shm.size = SHM_REGION_PAGE_SIZE;
        client->shm.mapped = true;
        printf("Client %d (instance %d): Memory mapped successfully at: %p\n", client->client_id, instance_index, client->shm.addr);
        return true;
    }
}
// Запись данных клиентом
bool client_write(struct ClientInstance *client, long offset, const char *text)
{
    // Проверка входных данных
    if (!client || !client->active || !text)
    {
        printf("Error: client_write invalid arguments (client=%p, active=%d, text=%p)\n",
               (void *)client, client ? client->active : 0, (void *)text);
        return false;
    }

    if (!client->shm.mapped)
    {
        printf("Client %d: Memory not mapped! Cannot write. Use 'client <index> mmap' first.\n", client->client_id);
        return false;
    }

    // Предупреждение, если клиент не подключен (сервер не получит уведомление)
    if (client->connected_server_name[0] == '\0')
    { // Проверяем по имени, т.к. ID может быть -1
        printf("Warning: Client %d is not connected to any server. Write will not notify server.\n", client->client_id);
    }

    // Проверка смещения
    if (offset < 0 || (size_t)offset >= client->shm.size)
    {
        printf("Error: Invalid offset %ld for client write (max %zu)\n", offset, client->shm.size - 1);
        return false;
    }

    // Вычисляем размер для записи
    size_t text_len = strlen(text);
    size_t available_space = client->shm.size - (size_t)offset;
    size_t write_len = (text_len < available_space) ? text_len : available_space;

    // Копируем данные
    memcpy((char *)client->shm.addr + offset, text, write_len);

    // Найдем индекс клиента в массиве для логгирования
    int instance_index = -1;
    for (int i = 0; i < MAX_INSTANCES; ++i)
        if (&g_clients[i] == client)
        {
            instance_index = i;
            break;
        }

    // Логируем запись
    if (write_len == text_len && write_len < available_space)
    {
        ((char *)client->shm.addr)[offset + write_len] = '\0';
        printf("Client %d (instance %d) wrote %zu bytes (null term) at offset %ld: \"%s\"\n",
               client->client_id, instance_index, write_len + 1, offset, text);
    }
    else
    {
        printf("Client %d (instance %d) wrote %s%zu bytes at offset %ld: \"%.*s%s\"\n",
               client->client_id, instance_index,
               (write_len < text_len) ? "truncated " : "",
               write_len, offset,
               (int)write_len, text, (write_len < text_len) ? "..." : "");
    }

    // Уведомляем драйвер об окончании записи клиентом
    u32 packed_client_id = pack_ids(client->client_id, 0); // Второй ID для клиента = 0
    if (packed_client_id == (u32)-EINVAL)
    {
        printf("Error: Failed to pack client ID for IOCTL_CLIENT_END_WRITING.\n");
    }
    else
    {
        // Отправляем IOCTL только если клиент к кому-то подключен
        if (client->connected_server_name[0] != '\0')
        {
            printf("Client %d: Sending IOCTL_CLIENT_END_WRITING (packed_id 0x%x) to notify server '%s'\n",
                   client->client_id, packed_client_id, client->connected_server_name);
            if (ioctl(g_dev_fd, IOCTL_CLIENT_END_WRITING, packed_client_id) < 0)
            {
                perror("IOCTL_CLIENT_END_WRITING failed");
            }
        }
        else
        {
            // Нет смысла отправлять ioctl, если не подключены
        }
    }

    return true;
}

// Чтение данных клиентом
bool client_read(struct ClientInstance *client, long offset, long length)
{
    // Проверка входных данных
    if (!client || !client->active)
    {
        printf("Error: client_read invalid arguments (client=%p, active=%d)\n",
               (void *)client, client ? client->active : 0);
        return false;
    }

    if (!client->shm.mapped)
    {
        printf("Client %d: Memory not mapped! Cannot read. Use 'client <index> mmap' first.\n", client->client_id);
        // Попытаться отобразить?
        printf("Attempting to mmap memory for client %d on demand for read...\n", client->client_id);
        if (!client_mmap(client))
        {
            printf("Failed to mmap memory for client %d on demand.\n", client->client_id);
            return false;
        }
        if (!client->shm.mapped)
        { // Проверка после попытки mmap
            printf("Error: Failed to map memory for client %d even after mmap attempt.\n", client->client_id);
            return false;
        }
    }

    // Проверка смещения и длины
    if (offset < 0 || length <= 0 || (size_t)offset >= client->shm.size)
    {
        printf("Error: Invalid offset (%ld) or length (%ld) for client read (max size %zu)\n", offset, length, client->shm.size);
        return false;
    }

    // Корректируем длину чтения, если она выходит за пределы
    size_t read_length = (size_t)length;
    if ((size_t)offset + read_length > client->shm.size)
    {
        read_length = client->shm.size - (size_t)offset;
        printf("Warning: Client read length truncated from %ld to %zu bytes to fit buffer.\n", length, read_length);
    }

    // Выделяем буфер для чтения + нуль-терминатор
    char *read_buf = malloc(read_length + 1);
    if (!read_buf)
    {
        perror("malloc failed for client read buffer");
        return false;
    }

    // Копируем данные и добавляем нуль-терминатор
    memcpy(read_buf, (char *)client->shm.addr + offset, read_length);
    read_buf[read_length] = '\0';

    // Найдем индекс клиента в массиве для логгирования
    int instance_index = -1;
    for (int i = 0; i < MAX_INSTANCES; ++i)
        if (&g_clients[i] == client)
        {
            instance_index = i;
            break;
        }

    printf("Client %d (instance %d) read at offset %ld (%zu bytes): \"%s\"\n",
           client->client_id, instance_index, offset, read_length, read_buf);
    free(read_buf);
    return true;
}

// Отображение информации о клиенте
void client_show(struct ClientInstance *client)
{
    if (!client)
    {
        printf("Error: client_show called with NULL client pointer.\n");
        return;
    }

    // Найдем индекс в массиве g_clients для user-friendly вывода
    int instance_index = -1;
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        if (&g_clients[i] == client)
        {
            instance_index = i;
            break;
        }
    }

    printf("--- Client Instance %d State ---\n", instance_index);
    if (!client->active)
    {
        printf("  Status:        Inactive\n");
        printf("-------------------------------\n");
        return;
    }
    printf("  Status:        Active\n");
    printf("  Registered ID: %d\n", client->client_id);
    printf("  Process PID:   %d\n", getpid());
    printf("  Connected to:  '%s'\n",
           client->connected_server_name[0] ? client->connected_server_name : "(Not Connected)");
    // ID сервера мы не знаем точно без доп. механизма, так что не показываем его
    // if (client->connected_server_id != -1) {
    //     printf("  Connected Server ID: %d\n", client->connected_server_id);
    // }
    printf("  Memory Mapped: %s\n", client->shm.mapped ? "Yes" : "No");
    if (client->shm.mapped)
    {
        printf("    Address: %p\n", client->shm.addr);
        printf("    Size:    %zu bytes\n", client->shm.size);
    }
    printf("-------------------------------\n");
}

// Очистка клиента
void client_cleanup(struct ClientInstance *client)
{
    if (!client || !client->active)
        return;

    printf("Cleaning up client instance (ID %d)...\n", client->client_id);
    //int client_id_copy = client->client_id; // Копируем ID
    bool was_active = client->active;

    // 1. Unmap память, если была отображена
    if (client->shm.mapped && client->shm.addr != MAP_FAILED)
    {
        printf("  Unmapping memory at %p (size %zu)\n", client->shm.addr, client->shm.size);
        if (munmap(client->shm.addr, client->shm.size) != 0)
        {
            perror("  munmap failed during client cleanup");
        }
        // Сбрасываем состояние в любом случае
        client->shm.mapped = false;
        client->shm.addr = MAP_FAILED;
        client->shm.size = 0;
    }

    // 2. TODO: Отправить IOCTL_UNREGISTER_CLIENT, если он реализован
    // if (ioctl(g_dev_fd, IOCTL_UNREGISTER_CLIENT, &client_id_copy) < 0) {
    //     // perror("  IOCTL_UNREGISTER_CLIENT failed (or not implemented)");
    // } else {
    //     printf("  Client ID %d unregistered request sent to kernel.\n", client_id_copy);
    // }

    // 3. Помечаем слот как неактивный
    client->active = false;
    client->client_id = -1; // Сбрасываем ID
    client->connected_server_name[0] = '\0';
    client->connected_server_id = -1;

    // 4. Уменьшаем глобальный счетчик, если был активен
    if (was_active)
    {
        g_num_clients--;
    }
    printf("Client instance cleanup complete.\n");
}

// --- Основной Цикл и Обработка Команд ---

void print_commands()
{
    printf("\nAvailable commands (use array index [0-%d]):\n", MAX_INSTANCES - 1);
    printf("  help                                       - Show this help\n");
    printf("  exit                                       - Clean up and exit\n");
    printf(" Client Commands:\n");
    printf("  client register                            - Register a new client instance\n");
    printf("  client <idx> connect <server_name>         - Connect client [idx] to named server\n");
    printf("  client <idx> mmap                          - Map shared memory for client [idx]\n");
    printf("  client <idx> show                          - Show status of client [idx]\n");
    printf("  client <idx> write <offset> <text>         - Write from client [idx] (sends IOCTL_CLIENT_END_WRITING)\n");
    printf("  client <idx> read <offset> <len>           - Read locally for client [idx]\n");
    printf(" Server Commands:\n");
    printf("  server register <name>                     - Register a new server instance\n");
    printf("  server <idx> mmap <shm_id>                 - Map sub-memory [shm_id] for server [idx]\n");
    printf("  server <idx> show                          - Show status of server [idx]\n");
    printf("  server <idx> write <client_id> <off> <txt> - Write from server [idx] to [client_id] (sends IOCTL_SERVER_END_WRITING)\n");
    printf("  server <idx> read <shm_id> <off> <len>     - Read locally for server [idx] from [shm_id]\n");
    printf(" Notes:\n");
    printf(" - <idx> refers to the instance index in the local g_clients/g_servers array [0-%d].\n", MAX_INSTANCES - 1);
    printf(" - <client_id> / <server_id> are IDs assigned by the kernel.\n");
    printf(" - <shm_id> is the sub-memory ID assigned by the kernel (used by server).\n");
    printf(" - Notifications (NEW_CONNECTION, NEW_MESSAGE) are handled in a separate thread.\n");
}

// Обработка введенной команды
void process_command(char *input)
{
    // Используем strtok_r для потокобезопасности (хотя здесь не строго нужно)
    char *saveptr;
    char *command = strtok_r(input, " \t\n", &saveptr);
    char *arg1 = strtok_r(NULL, " \t\n", &saveptr);
    char *arg2 = strtok_r(NULL, " \t\n", &saveptr);
    char *arg3 = strtok_r(NULL, " \t\n", &saveptr);
    // arg4 берет остаток строки, удаляем ведущие пробелы
    char *arg4 = strtok_r(NULL, "\n", &saveptr);
    if (arg4)
        while (*arg4 && isspace((unsigned char)*arg4))
            arg4++;
    if (arg4 && *arg4 == '\0')
        arg4 = NULL;

    if (!command)
        return; // Пустая строка

    int index = -1;  // Индекс в массиве g_clients/g_servers
    int id_arg = -1; // Для ID (client_id, shm_id)
    long offset = -1;
    long length = -1; // Для client_read, server_read

    /**
     * CLIENT COMMANDS
     */
    if (strcmp(command, "client") == 0)
    {
        if (!arg1)
        {
            printf("Client command requires arguments. Use 'help'.\n");
            return;
        }

        if (strcmp(arg1, "register") == 0)
        {
            client_register();
        }
        // Команды, требующие индекса клиента
        else if (parse_index(arg1, &index, MAX_INSTANCES))
        {
            struct ClientInstance *client = find_client_instance(index);
            if (!client)
            { // Проверяем, что find вернул не NULL
                printf("Error: Client instance %d is not active or does not exist. Use 'client register'.\n", index);
                return;
            }
            // Теперь передаем 'client' вместо 'index'
            if (!arg2)
            {
                printf("Client command for index %d requires a second argument (connect, mmap, etc.).\n", index);
                return;
            }

            if (strcmp(arg2, "connect") == 0)
            {
                if (!arg3)
                {
                    printf("Usage: client <idx> connect <server_name>\n");
                    return;
                }
                client_connect(client, arg3); // Передаем указатель
            }
            else if (strcmp(arg2, "mmap") == 0)
            {
                client_mmap(client); // Передаем указатель
            }
            else if (strcmp(arg2, "show") == 0)
            {
                client_show(client); // Передаем указатель
            }
            else if (strcmp(arg2, "write") == 0)
            {
                if (!arg3 || !arg4)
                {
                    printf("Usage: client <idx> write <offset> <text>\n");
                    return;
                }
                char *endptr_off;
                offset = strtol(arg3, &endptr_off, 10);
                if (endptr_off == arg3 || *endptr_off != '\0' || offset < 0)
                {
                    printf("Error: Invalid offset '%s'. Must be a non-negative integer.\n", arg3);
                    return;
                }
                client_write(client, offset, arg4); // Передаем указатель
            }
            else if (strcmp(arg2, "read") == 0)
            {
                if (!arg3 || !arg4)
                {
                    printf("Usage: client <idx> read <offset> <length>\n");
                    return;
                }
                char *endptr_off, *endptr_len;
                offset = strtol(arg3, &endptr_off, 10);
                length = strtol(arg4, &endptr_len, 10);
                if (endptr_off == arg3 || *endptr_off != '\0' || offset < 0)
                {
                    printf("Error: Invalid offset '%s'. Must be a non-negative integer.\n", arg3);
                    return;
                }
                if (endptr_len == arg4 || *endptr_len != '\0' || length <= 0)
                {
                    printf("Error: Invalid length '%s'. Must be a positive integer.\n", arg4);
                    return;
                }
                client_read(client, offset, length); // Передаем указатель
            }
            else
            {
                printf("Unknown client command for index %d: '%s'\n", index, arg2);
            }
        }
        else
        {
            printf("Invalid client command or index: '%s'. Use 'client register' or 'client <index> ...'.\n", arg1);
        }
    }
    /**
     * SERVER COMMANDS
     */
    else if (strcmp(command, "server") == 0)
    {
        if (!arg1)
        {
            printf("Server command requires arguments. Use 'help'.\n");
            return;
        }

        if (strcmp(arg1, "register") == 0)
        {
            if (!arg2)
            {
                printf("Usage: server register <name>\n");
                return;
            }
            server_register(arg2);
        }
        // Команды, требующие индекса сервера
        else if (parse_index(arg1, &index, MAX_INSTANCES))
        {
            struct ServerInstance *server = find_server_instance(index);
            if (!server)
            { // Проверяем find
                printf("Error: Server instance %d is not active or does not exist. Use 'server register <name>'.\n", index);
                return;
            }
            // Теперь передаем 'server' вместо 'index'
            if (!arg2)
            {
                printf("Server command for index %d requires a second argument (mmap, show, etc.).\n", index);
                return;
            }

            if (strcmp(arg2, "mmap") == 0)
            {
                // parse_index для ID (shm_id)
                if (!arg3 || !parse_index(arg3, &id_arg, MAX_ID_VALUE + 1) || !IS_ID_VALID(id_arg))
                {
                    printf("Usage: server <idx> mmap <shm_id> (shm_id must be a valid ID)\n");
                    return;
                }
                server_mmap(server, id_arg); // Передаем указатель
            }
            else if (strcmp(arg2, "show") == 0)
            {
                server_show(server); // Передаем указатель
            }
            else if (strcmp(arg2, "write") == 0)
            {
                // Формат: server <idx> write <client_id> <offset> <text>
                // arg3 = client_id, arg4 = "<offset> <text>"
                // parse_index для ID (client_id)
                if (!arg3 || !arg4 || !parse_index(arg3, &id_arg, MAX_ID_VALUE + 1) || !IS_ID_VALID(id_arg))
                {
                    printf("Usage: server <idx> write <client_id> <offset> <text> (client_id must be a valid ID)\n");
                    return;
                }

                // Разбираем offset и text из arg4
                char *p_offset_end;
                char *offset_str = arg4;
                char *text_start = strchr(offset_str, ' '); // Находим первый пробел

                if (!text_start || text_start == offset_str)
                { // Нет пробела или пробел в начале
                    printf("Usage: server <idx> write <client_id> <offset> <text> (Missing offset or text)\n");
                    return;
                }

                *text_start = '\0'; // Временно терминируем строку offset
                text_start++;       // Указываем на начало текста (или пробелов перед ним)
                while (*text_start && isspace((unsigned char)*text_start))
                    text_start++; // Пропускаем пробелы

                // Проверяем offset
                offset = strtol(offset_str, &p_offset_end, 10);
                if (errno != 0 || p_offset_end != (offset_str + strlen(offset_str)) || offset < 0)
                {
                    *(text_start - 1) = ' '; // Восстанавливаем пробел
                    printf("Error: Invalid offset '%s'. Must be a non-negative integer.\n", offset_str);
                    return;
                }
                // Проверяем, есть ли текст
                if (*text_start == '\0')
                {
                    *(text_start - 1) = ' '; // Восстанавливаем пробел
                    printf("Usage: server <idx> write <client_id> <offset> <text> (Missing text after offset)\n");
                    return;
                }

                server_write(server, id_arg, offset, text_start); // Передаем указатель
            }
            else if (strcmp(arg2, "read") == 0)
            {
                // Формат: server <idx> read <shm_id> <offset> <length>
                // parse_index для ID (shm_id)
                if (!arg3 || !arg4 || !parse_index(arg3, &id_arg, MAX_ID_VALUE + 1) || !IS_ID_VALID(id_arg))
                {
                    printf("Usage: server <idx> read <shm_id> <offset> <length> (shm_id must be a valid ID)\n");
                    return;
                }

                // Разбираем offset и length из arg4
                char *p_offset_end, *p_length_end;
                char *offset_str = arg4;
                char *length_str = strchr(offset_str, ' ');
                if (!length_str || length_str == offset_str)
                {
                    printf("Usage: server <idx> read <shm_id> <offset> <length> (Missing offset or length)\n");
                    return;
                }
                *length_str = '\0';
                length_str++;
                while (*length_str && isspace((unsigned char)*length_str))
                    length_str++;

                offset = strtol(offset_str, &p_offset_end, 10);
                length = strtol(length_str, &p_length_end, 10);

                if (errno != 0 || p_offset_end != (offset_str + strlen(offset_str)) || offset < 0)
                {
                    *(length_str - 1) = ' '; // Восстанавливаем
                    printf("Error: Invalid offset '%s'. Must be a non-negative integer.\n", offset_str);
                    return;
                }
                if (errno != 0 || length_str == p_length_end || *p_length_end != '\0' || length <= 0 || *length_str == '\0')
                {
                    *(length_str - 1) = ' '; // Восстанавливаем
                    printf("Error: Invalid length '%s'. Must be a positive integer.\n", length_str);
                    return;
                }

                // Находим нужный submem
                struct ServerShmMapping *submem = server_find_submem_by_id(server, id_arg);
                if (!submem)
                {
                    printf("Error: Server %d has no mapping for shm_id %d.\n", server->server_id, id_arg);
                    *(length_str - 1) = ' '; // Восстанавливаем строку arg4
                    return;
                }
                // Вызываем server_read с указателями
                server_read(server, submem, offset, length);
            }
            else
            {
                printf("Unknown server command for index %d: '%s'\n", index, arg2);
            }
        }
        else
        {
            printf("Invalid server command or index: '%s'. Use 'server register <name>' or 'server <index> ...'.\n", arg1);
        }
    }
    /**
     * HELP COMMAND
     */
    else if (strcmp(command, "help") == 0)
    {
        print_commands();
    }
    /**
     * EXIT COMMAND (handled in main loop)
     */
    else if (strcmp(command, "exit") == 0)
    {
        // g_running будет установлен в false в main
    }
    /**
     * UNKNOWN COMMAND
     */
    else
    {
        printf("Unknown command: '%s'. Type 'help' for available commands.\n", command);
    }
}

// --- Очистка и Завершение ---
void cleanup_all()
{
    printf("Initiating cleanup...\n");

    // 1. Сигнализируем потоку-слушателю о завершении
    g_running = false;

    // 2. Ожидаем завершения потока-слушателя
    if (g_listener_tid != 0)
    {
        printf("Waiting for notification listener thread (TID: %lu) to join...\n", (unsigned long)g_listener_tid);
        if (pthread_join(g_listener_tid, NULL) != 0)
        {
            perror("Failed to join notification listener thread");
            // Продолжаем очистку, но поток может остаться
        }
        else
        {
            printf("Notification listener thread joined successfully.\n");
        }
        g_listener_tid = 0; // Сбрасываем TID
    }
    else
    {
        printf("Notification listener thread was not running or already joined.\n");
    }

    // 3. Очищаем все активные экземпляры клиентов
    printf("Cleaning up client instances (%d potentially active)...\n", g_num_clients);
    int cleaned_clients = 0;
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        if (g_clients[i].active)
        {                                  // Проверяем флаг активности перед очисткой
            client_cleanup(&g_clients[i]); // Передаем указатель
            cleaned_clients++;
        }
    }
    printf("Cleaned up %d client instances.\n", cleaned_clients);
    if (cleaned_clients != g_num_clients)
    { // Проверка счетчика
        printf("Warning: Mismatch in active client count (g_num_clients=%d, cleaned=%d)\n", g_num_clients, cleaned_clients);
    }

    // 4. Очищаем все активные экземпляры серверов
    printf("Cleaning up server instances (%d potentially active)...\n", g_num_servers);
    int cleaned_servers = 0;
    for (int i = 0; i < MAX_INSTANCES; ++i)
    {
        if (g_servers[i].active)
        {                                  // Проверяем флаг активности
            server_cleanup(&g_servers[i]); // Передаем указатель
            cleaned_servers++;
        }
    }
    printf("Cleaned up %d server instances.\n", cleaned_servers);
    if (cleaned_servers != g_num_servers)
    { // Проверка счетчика
        printf("Warning: Mismatch in active server count (g_num_servers=%d, cleaned=%d)\n", g_num_servers, cleaned_servers);
    }

    // 5. Закрываем файловый дескриптор устройства
    if (g_dev_fd >= 0)
    {
        printf("Closing device '%s' (fd=%d)...\n", DEVICE_PATH, g_dev_fd);
        if (close(g_dev_fd) != 0)
        {
            perror("Failed to close device fd");
        }
        else
        {
            printf("Device closed successfully.\n");
        }
        g_dev_fd = -1; // Сбрасываем дескриптор
    }
    else
    {
        printf("Device fd was already closed or not opened.\n");
    }

    printf("Cleanup finished.\n");
}

// --- Точка Входа ---
int main()
{
    char input[MAX_INPUT_LEN];

    printf("RIPC Multi-Instance Test Application (PID: %d)\n", getpid());
    printf("Initializing...\n");

    initialize_instances(); // Инициализация глобальных структур

    if (!open_device())
    { // Открытие устройства
        fprintf(stderr, "Failed to open device '%s'. Exiting.\n", DEVICE_PATH);
        return 1;
    }

    get_page_size(); // Определение размера страницы

    // Запуск потока-слушателя уведомлений
    if (!setup_signal_handler())
    {
        fprintf(stderr, "Failed to set up notification listener. Exiting.\n");
        cleanup_all(); // Выполняем частичную очистку (закрытие fd)
        return 1;
    }

    printf("Initialization complete. Type 'help' for commands.\n");

    // Основной цикл обработки команд пользователя
    while (g_running)
    {
        printf("> ");
        fflush(stdout); // Убедимся, что '>' отобразился

        if (!fgets(input, MAX_INPUT_LEN, stdin))
        {
            // Обработка конца файла или ошибки чтения
            if (feof(stdin))
            {
                printf("\nEOF detected. Exiting...\n");
                g_running = false;
            }
            else if (ferror(stdin))
            {
                perror("\nfgets failed");
                g_running = false; // Выходим при ошибке
                clearerr(stdin);   // Сбросить состояние ошибки
            }
            else if (errno == EINTR)
            { // Маловероятно, но возможно
                printf("\nfgets interrupted. Retrying...\n");
                clearerr(stdin);
                errno = 0;
                continue; // Повторить итерацию цикла
            }
            else
            { // Неожиданное завершение fgets
                printf("\nfgets returned NULL unexpectedly. Exiting...\n");
                g_running = false;
            }
            continue; // Переходим к проверке g_running и выходу/очистке
        }

        // Убираем символ новой строки, если он есть
        input[strcspn(input, "\n")] = 0;

        // Игнорируем пустые строки или строки только из пробелов
        if (strspn(input, " \t") == strlen(input))
        {
            continue;
        }

        // Проверяем на команду exit до вызова strtok_r
        // Сравнение должно быть точным "exit" или "exit "
        if (strcmp(input, "exit") == 0 || strncmp(input, "exit ", 5) == 0)
        {
            printf("Exit command received.\n");
            g_running = false; // Устанавливаем флаг для выхода из цикла
            continue;
        }

        // Обрабатываем введенную команду
        process_command(input);

    } // end while(g_running)

    // Очистка перед выходом
    cleanup_all();

    printf("Application finished.\n");
    return 0;
}