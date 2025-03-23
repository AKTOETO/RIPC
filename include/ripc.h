#ifndef RIPC_H
#define RIPC_H

#include <linux/ioctl.h>

/* Константы драйвера */
#define DEVICE_NAME "ripc"        // Имя устройства в /dev
#define CLASS_NAME "ripc"         // имя класса устройств
#define MAX_SERVER_NAME 64        // Максимальная длина имени сервера
#define SHARED_MEM_SIZE PAGE_SIZE // Размер общей памяти (1 страница)

/*
 *  IOCTL commands
 */
#define IOCTL_MAGIC '/'
#define IOCTL_REGISTER_SERVER _IOW(IOCTL_MAGIC, 1, char *) // регистрация сервера в системе



#define IOCTL_MAX_NUM 1 // максимальное количество команд


#endif // RIPC_H