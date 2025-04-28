#define _GNU_SOURCE  // signalfd
#include "ripc.h"    // Ваш заголовок с IOCTL и структурами
#include "id_pack.h" // Ваш заголовок для упаковки/распаковки ID
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h> // длябоработки сигналов
#include <sys/mman.h>     // для mmap и макросов его
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

// --- Константы ---
#define MAX_INSTANCES 16       // Макс. кол-во клиентов + серверов в одном процессе (всего 32 сущности)
#define MAX_SERVER_CLIENTS 16  // максимальное количество клиентов, подключенных к серверу
#define MAX_SERVER_SHM 16      // Максимальное количество общих памятей на сервер
#define MAX_INPUT_LEN 256      // максимальная длина вводимой строки в консольном режиме
#define MAX_HANDLED_SIGNALS 10 // Максимальное количество сигналов для обработки

// --- Тип Указателя на Функцию-Обработчик Сигналов ---
typedef void (*signal_handler_func_t)(const struct signalfd_siginfo *fdsi);

// --- Структуры ---

// Описание обработчика сигнала
struct SignalDispatcher
{
    int signo;                     // номер сигнала
    signal_handler_func_t handler; // указатель на обработчик сигнала
};

// Информация о связи клиента и памяти
struct ClientConnectionInfo
{
    int client_id;
    int shm_id;
    bool active;
};

// Информация об отображенной сервером памяти
struct ServerShmMapping
{
    int shm_id;
    void *addr;
    size_t size;
    bool mapped;
};

// Структура для хранения состояния ОДНОГО экземпляра сервера
struct ServerInstance
{
    char name[MAX_SERVER_NAME];
    int server_id; // ID, полученный от ядра
    struct ClientConnectionInfo connections[MAX_SERVER_CLIENTS];
    struct ServerShmMapping mappings[MAX_SERVER_SHM];
    int num_connections;
    int num_mappings;
    bool active; // Активен ли этот слот экземпляра
};

// Структура для хранения состояния ОДНОГО экземпляра клиента
struct ClientInstance
{
    int client_id; // ID, полученный от ядра
    // pid_t pid;
    char connected_server_name[MAX_SERVER_NAME]; // К какому серверу подключен
    int connected_server_id;                     // ID сервера (если известен) - пока не используется
    struct
    { // Инкапсулируем информацию о памяти клиента
        void *addr;
        size_t size;
        bool mapped;
    } shm;
    bool active; // Активен ли этот слот экземпляра
};

// --- Глобальные переменные ---
// работаем ли мы еще
volatile bool g_running = 0;
// файловый дескриптор для обработки сигналов
volatile int g_signalfd = -1;
// поток-слушатель
extern pthread_t g_listener_tid;

// массив обработчиков сигналов
volatile struct SignalDispatcher g_signal_dispatch_table[MAX_HANDLED_SIGNALS];

// TODO: убрать, это устаревший код
// volatile sig_atomic_t g_signal_received_flag = 0;
// volatile int g_signal_client_id = -1;
// volatile int g_signal_shm_id = -1;

// Массивы для хранения экземпляров
struct ServerInstance g_servers[MAX_INSTANCES];
struct ClientInstance g_clients[MAX_INSTANCES];
int g_num_servers = 0; // Счетчик активных серверов (не индекс!)
int g_num_clients = 0; // Счетчик активных клиентов

long g_page_size = 4096; // Размер страницы (определим в main)
int g_dev_fd = -1;       // Глобальный файловый дескриптор устройства

// --- Прототипы функций ---

// --- обработчик сигналов ---
// void signal_handler(int sig, siginfo_t *info, void *ucontext);
// обработчик сигнала NEW_CONNECTION
void handle_new_connection(const struct signalfd_siginfo *fdsi);
// обработчик сигнала NEW_MESSAGE
void handle_new_message(const struct signalfd_siginfo *fdsi);

// Функция поиска и вызова обработчика
void dispatch_signal(const struct signalfd_siginfo *fdsi);

// --- Поток для Ожидания и Обработки Сигналов ---
void *signal_listener_thread(void *arg);

// Инициализация
void initialize_instances();
bool setup_signal_handler();
bool open_device();
void get_page_size();

// получение индекса из строки
bool parse_index(const char *arg, int *index, int max_val);

// поиск сервера по индексу
struct ServerInstance *find_server_by_driver_id(int id);
struct ServerInstance *find_server_instance(int index);
// поиск клиента по индексу
struct ClientInstance *find_client_instance(int index);
int server_find_shm_mapping_index(struct ServerInstance *server, int shm_id);
struct ServerShmMapping *server_find_submem_by_id(struct ServerInstance *server, int sub_mem_id);
struct ServerShmMapping *server_create_shm_mapping(struct ServerInstance *server, int sub_mem_id);
int server_find_connection_index(struct ServerInstance *server, int client_id);
void server_add_connection(struct ServerInstance *server, int client_id, int shm_id);

// Действия с сервером
bool server_register(const char *name);
bool server_mmap(int index, int shm_id);
bool server_write(int index, int client_id, long offset, const char *text);
// bool server_read(int index, int client_id, long offset, long length);
bool server_read(struct ServerInstance *server, struct ServerShmMapping *submem, long offset, long length);
void server_show(int index);
void server_cleanup(struct ServerInstance *server);

// Действия с клиентом
bool client_register();
bool client_connect(int index, const char *server_name);
bool client_mmap(int index);
bool client_write(int index, long offset, const char *text);
// bool client_read(int index, long offset, long length);
bool client_read(struct ClientInstance *client, long offset, long length);
void client_show(int index);
void client_cleanup(struct ClientInstance *client);

// Основной цикл
void print_commands();
// void process_signal();
void process_command(char *input);
void cleanup_all();