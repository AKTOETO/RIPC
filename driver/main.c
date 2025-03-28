#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/atomic.h> // атомарные операции
#include <linux/io.h>     // Добавлено для virt_to_phys
#include <linux/cdev.h>   // Символьные устройства cdev
#include <asm/ioctl.h>       // Для доп проверок в ioctl
#include "../include/ripc.h" // константы для драйвера
#include "err.h"             // макросы для логов
#include "connection.h"      // объект соединения
#include "client.h"
#include "server.h"
#include "shm.h"

// Переменные для регистрации устройства
static int g_major = 0;           // номер устройства (major)
static int g_minor = 0;           // номер устройства (minor)
static int g_dev_count = 1;       // количество устройств
static dev_t g_dev_num;           // Номер устройства (major+minor)
static struct class *g_dev_class; // Класс устройства
static struct cdev g_cdev;        // Структура символьного устройства


/**
 * Обработчик ioctl()
 */
static long ipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    //int ret = 0;
    struct server_t *server = NULL;
    struct client_t *client = NULL;
    //struct client_t *client2 = NULL;
    //struct shm_t *shm = NULL;

    // для регистрации сервера
    struct server_registration reg;

    // для подключения клиента к серверу
    struct connect_to_server con;

    // првоерка типа и номеров битовых полей, чтобы не декодировать неверные команды
    if (_IOC_TYPE(cmd) != IOCTL_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > IOCTL_MAX_NUM)
        return -ENOTTY;

    // проверка команды
    switch (cmd)
    {
        // регистрация нового сервера
    case IOCTL_REGISTER_SERVER:
        // копируем имя из userspace
        if (copy_from_user(&reg, (void __user *)arg, sizeof(reg)))
        {
            ERR("REGISTER_SERVER: copy_from_user failed\n");
            return -EFAULT;
        }

        // ищем сервер по имени
        server = find_server_by_name(reg.name);
        if (server)
        {
            ERR("REGISTER_SERVER: server already exists: %d:%s", server->m_id, server->m_name);
            return -EEXIST;
        }

        // создаем сервер
        server = server_create(reg.name);

        reg.server_id = server->m_id;

        // отправляем id обратно в userspace
        if (copy_to_user((void __user *)arg, &reg, sizeof(reg)))
        {
            ERR("REGISTER_SERVER: cant sand back server's id: %d:%s", server->m_id, server->m_name);
            return -EFAULT;
        }

        INF("REGISTER_SERVER: New server is registered: %d:%s", server->m_id, server->m_name);
        break;

        // регистрация нового клиента
    case IOCTL_REGISTER_CLIENT:

        // создали новго клиента
        client = client_create();

        // отправляем id обратно в userspace
        if (copy_to_user((void __user *)arg, &client->m_id, sizeof(client->m_id)))
        {
            ERR("REGISTER_CLIENT: cant sand back clients's id: %d", client->m_id);
            return -EFAULT;
        }

        INF("REGISTER_CLIENT: New client is registered: %d", client->m_id);
        break;

        // подключение клиента к серверу
    case IOCTL_CONNECT_TO_SERVER:
        // копируем запрос поделючения к серверу из userspace
        if (copy_from_user(&con, (void __user *)arg, sizeof(con)))
        {
            ERR("CONNECT_TO_SERVER: copy_from_user failed\n");
            return -EFAULT;
        }

        // ищем клиента с con.id
        client = find_client_by_id(con.client_id);

        // если клиент подключен к серверу, то выходим
        if (client->m_conn_p)
        {
            INF("CONNECT_TO_SERVER: client %d already connected to server %s",
                con.client_id, con.server_name);
            return -EEXIST;
        }

        // Клиент не подключен никуда

        // ищем сервер с подходящим именем
        server = find_server_by_name(con.server_name);

        // если не нашли сервер
        if (!server)
        {
            ERR("CONNECT_TO_SERVER: there is no server with name: %s", con.server_name);
            return -ENODATA;
        }

        // подключение клиента к серверу
        connect_client_to_server(server, client);

        break;

    default:
        INF("Unknown ioctl command: 0x%x", cmd);
        return -ENOTTY;
    }
    return 0;
}

static int ipc_mmap(struct file *file, struct vm_area_struct *vma)
{
    // return remap_pfn_range(vma, vma->vm_start, virt_to_phys(shared_buffer) >> PAGE_SHIFT,
    //                        vma->vm_end - vma->vm_start, vma->vm_page_prot);
    return (long)0;
}

// Операции файла драйвера
static struct file_operations g_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = ipc_ioctl,
    .mmap = ipc_mmap,
};

// обработчик прав
static char *devnode(const struct device *dev, umode_t *mode)
{
    // Установка прав доступа 0666
    if (mode)
        *mode = 0666;

    return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static int __init ipc_init(void)
{
    int result;

    // Выделение диапазона устройств
    result = alloc_chrdev_region(&g_dev_num, g_minor, g_dev_count, DEVICE_NAME);
    if (result < 0)
    {
        ERR("Failed to allocate char device region");
        return result;
    }
    g_major = MAJOR(g_dev_num);

    // Создание класса устройств
    g_dev_class = class_create(CLASS_NAME);
    if (IS_ERR(g_dev_class))
    {
        result = PTR_ERR(g_dev_class);
        ERR("Failed to create class: %d", result);
        goto class_fail;
    }
    // присваиваем обработчик прав
    g_dev_class->devnode = &devnode;

    // Создание устройства /dev/ripc_ipc c правами (rw-rw-rw-)
    if (!device_create(g_dev_class, NULL, g_dev_num, NULL, DEVICE_NAME))
    {
        ERR("Failed to create device");
        result = -ENOMEM;
        goto device_fail;
    }

    // Инициализация структуры cdev
    cdev_init(&g_cdev, &g_fops);
    result = cdev_add(&g_cdev, g_dev_num, g_dev_count);
    if (result)
    {
        ERR("Failed to create cdev: %d", result);
        goto cdev_fail;
    }

    INF("driver loaded");
    return 0;

    // удаление устройства при ошибке
cdev_fail:
    device_destroy(g_dev_class, g_dev_num);

    // удаление класса при ошибке
device_fail:
    class_destroy(g_dev_class);

    // удаление диапазона при ошибке
class_fail:
    unregister_chrdev_region(g_dev_num, g_dev_count);

    return result;
}

static void __exit ipc_exit(void)
{
    // удаление глобальных списков
    delete_shm_list();
    delete_server_list();
    delete_client_list();

    // удаление структур драйвера
    cdev_del(&g_cdev);
    device_destroy(g_dev_class, g_dev_num);
    class_destroy(g_dev_class);
    unregister_chrdev_region(g_dev_num, g_dev_count);

    INF("driver unloaded");
}

module_init(ipc_init);
module_exit(ipc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bogdan");
MODULE_DESCRIPTION("Driver for RESTful ipc");