#ifndef RIPC_H
#define RIPC_H

#include "id_pack.h"
#include <linux/ioctl.h>

#ifndef __KERNEL__
#define PAGE_SIZE 4096
#endif

/* Константы драйвера */
#define DEVICE_NAME "ripc" // Имя устройства в /dev
#define CLASS_NAME "ripc"  // имя класса устройств
#define DEVICE_PATH "/dev/" DEVICE_NAME

/**
 * Константы для работы с памятью
 */

#define MAX_SERVER_NAME 64                                            // Максимальная длина имени сервера
#define SHM_REGION_ORDER 0                                            // Порядок для alloc_pages (2^0 = 1 страница)
#define SHM_REGION_PAGE_NUMBER (1 << SHM_REGION_ORDER)                // Количество страниц в области (1)
#define SHM_REGION_PAGE_SIZE (SHM_REGION_PAGE_NUMBER * PAGE_SIZE)     // Размер памяти на область в байтах
#define SHM_POOL_SIZE 4                                               // Количество областей в пуле
#define SHM_POOL_PAGE_NUMBER (SHM_POOL_SIZE * SHM_REGION_PAGE_NUMBER) // Количество страниц памяти на пул
#define SHM_POOL_BYTE_SIZE (SHM_REGION_PAGE_SIZE * SHM_POOL_SIZE)     // Размер пула памяти в байтах

/**
 * Константы для ограничений на процесс
 */

#define MAX_PROCESSES 16
#define MAX_SERVERS_PER_PID 16
#define MAX_CLIENTS_PER_SERVER 16
#define MAX_CLIENTS_PER_PID 16

/**
 * NOTIFICATION
 */

// --- Типы отправителя ---
enum notif_sender
{
    SENDER_MIN,
    SERVER,
    CLIENT,
    SENDER_MAX
};
#define IS_NTF_SEND_VALID(sender) (sender > SENDER_MIN && sender < SENDER_MAX)

// --- Типы сигналов ---
enum notif_type
{
    TYPE_MIN,
    NEW_CONNECTION,
    NEW_MESSAGE,
    REMOTE_DISCONNECT,
    TYPE_MAX
};
#define IS_NTF_TYPE_VALID(sender) (sender > TYPE_MIN && sender < TYPE_MAX)

// Данные уведомления
struct notification_data
{
    short m_who_sends; // кто отправитель: 1-сервер; 0-клиент
    short m_type;      // тип сигнала
    int m_sub_mem_id;
    int m_sender_id;
    int m_reciver_id;
};

#define IS_NTF_DATA_VALID(ntf)                                                                                         \
    (IS_NTF_TYPE_VALID(ntf.m_type) && IS_NTF_SEND_VALID(ntf.m_who_sends) && IS_ID_VALID(ntf.m_reciver_id) &&           \
     IS_ID_VALID(ntf.m_sub_mem_id) && IS_ID_VALID(ntf.m_sender_id))

/**
 * Структуры данных для утилиты мониторинга ripcctl
 */
struct st_client
{
    int id;
    int srv_id;
};

struct st_server
{
    int id;
    char name[MAX_SERVER_NAME];
    int conn_ids[MAX_CLIENTS_PER_SERVER];
    int conn_count;
};

struct st_task
{
    int pid;
    struct st_client clients[MAX_CLIENTS_PER_PID];
    struct st_server servers[MAX_SERVERS_PER_PID];
    int clients_count;
    int servers_count;
};

struct st_reg_tasks
{
    struct st_task tasks[MAX_PROCESSES];
    int tasks_count;
};

/**
 * IOCTL data for commands
 */

// IOCTL REGISTER_SERVER
struct server_registration
{
    char name[MAX_SERVER_NAME];
    int server_id;
};

// IOCTL CONNECT_TO_SERVER
struct connect_to_server
{
    int client_id;
    char server_name[MAX_SERVER_NAME];
};

/*
 *  IOCTL commands
 */
#define IOCTL_MAGIC '/'
#define IOCTL_REGISTER_SERVER _IOWR(IOCTL_MAGIC, 1, struct server_registration) // регистрация сервера в системе
#define IOCTL_REGISTER_CLIENT _IOR(IOCTL_MAGIC, 2, int)                         // регистрация клиента
#define IOCTL_CONNECT_TO_SERVER _IOW(IOCTL_MAGIC, 3, struct connect_to_server)  // подключение к серверу
#define IOCTL_CLIENT_END_WRITING                                                                                       \
    _IOW(IOCTL_MAGIC, 4, unsigned int) // оповещение драйвера об окончании записи из клиента
#define IOCTL_SERVER_END_WRITING                                                                                       \
    _IOW(IOCTL_MAGIC, 5, unsigned int) // оповещение драйвера об окончании записи из сервера
#define IOCTL_CLIENT_DISCONNECT                                                                                        \
    _IOW(IOCTL_MAGIC, 6, unsigned int) // Запрос от клиента на отключение от сервера (client_id, 0)
#define IOCTL_SERVER_DISCONNECT                                                                                        \
    _IOW(IOCTL_MAGIC, 7, unsigned int) // Запрос от сервера на отключение от клиента (server_id, client_id)
#define IOCTL_CLIENT_UNREGISTER _IOW(IOCTL_MAGIC, 8, unsigned int) // Запрос от клиента на полное отключение (client_id)
#define IOCTL_SERVER_UNREGISTER _IOW(IOCTL_MAGIC, 9, unsigned int) // Запрос от сервера на полное отключение (server_id)
#define IOCTL_REGISTER_MONITOR _IO(IOCTL_MAGIC, 10)                // запрос регистрации монитора

#define IOCTL_MAX_NUM 10 // максимальное количество команд

#endif // RIPC_H