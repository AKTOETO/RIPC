#ifndef RIPC_H
#define RIPC_H

#include <linux/ioctl.h>

/* Константы драйвера */
#define DEVICE_NAME "ripc"        // Имя устройства в /dev
#define CLASS_NAME "ripc"         // имя класса устройств
#define MAX_SERVER_NAME 64        // Максимальная длина имени сервера
#define SHARED_MEM_SIZE PAGE_SIZE // Размер общей памяти (1 страница)
#define MAX_SHARED_MEM_SIZE SHARED_MEM_SIZE // Максимально возможный размер общей памяти
#define DEVICE_PATH "/dev/" DEVICE_NAME

/**
 * IOCTL data for commands
 */

// IOCTL REGISTER_SERVER
struct server_registration
{
    char name[MAX_SERVER_NAME];
    int server_id;
};

/*
 *  IOCTL commands
 */
#define IOCTL_MAGIC '/'
#define IOCTL_REGISTER_SERVER _IOWR(IOCTL_MAGIC, 1, struct server_registration) // регистрация сервера в системе
#define IOCTL_REGISTER_CLIENT _IOR(IOCTL_MAGIC, 2, int)  // регистрация клиента

#define IOCTL_MAX_NUM 2 // максимальное количество команд

#endif // RIPC_H