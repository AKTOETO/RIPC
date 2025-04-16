#ifndef RIPC_H
#define RIPC_H

#include <linux/ioctl.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/* Константы драйвера */
#define DEVICE_NAME "ripc"                  // Имя устройства в /dev
#define CLASS_NAME "ripc"                   // имя класса устройств
#define MAX_SERVER_NAME 64                  // Максимальная длина имени сервера
#define DEVICE_PATH "/dev/" DEVICE_NAME

/**
 * Константы для работы с памятью
 */

#define SHM_REGION_ORDER 0                                            // Порядок для alloc_pages (2^0 = 1 страница)
#define SHM_REGION_PAGE_NUMBER (1 << SHM_REGION_ORDER)                // Количество страниц в области (1)
#define SHM_REGION_PAGE_SIZE (SHM_REGION_PAGE_NUMBER * PAGE_SIZE)     // Размер памяти на область в байтах
#define SHM_POOL_SIZE 4                                               // Количество областей в пуле
#define SHM_POOL_PAGE_NUMBER (SHM_POOL_SIZE * SHM_REGION_PAGE_NUMBER) // Количество страниц памяти на пул
#define SHM_POOL_BYTE_SIZE (SHM_REGION_PAGE_SIZE * SHM_POOL_SIZE)     // Размер пула памяти в байтах
// #define SHARED_MEM_SIZE PAGE_SIZE           // Размер общей памяти (1 страница)

/**
 * SIGNALS
 */

#define NEW_CONNECTION SIGUSR1

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

// SIGNAL NEW_CONNECTION
struct new_connection
{
    int client_pid;
    int client_id;
};

/*
 *  IOCTL commands
 */
#define IOCTL_MAGIC '/'
#define IOCTL_REGISTER_SERVER   _IOWR(IOCTL_MAGIC, 1, struct server_registration) // регистрация сервера в системе
#define IOCTL_REGISTER_CLIENT   _IOR (IOCTL_MAGIC, 2, int)                        // регистрация клиента
#define IOCTL_CONNECT_TO_SERVER _IOW (IOCTL_MAGIC, 3, struct connect_to_server)   // подключение к серверу

#define IOCTL_MAX_NUM 3 // максимальное количество команд

#endif // RIPC_H