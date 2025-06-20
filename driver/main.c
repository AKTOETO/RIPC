#include "../include/ripc.h" // константы для драйвера
#include "client.h"
#include "connection.h" // объект соединения
#include "err.h"        // макросы для логов
#include "server.h"
#include "shm.h"
#include "task.h"
#include <asm/ioctl.h>    // Для доп проверок в ioctl
#include <asm/pgtable.h>  // макросы для работы с таблицей страниц
#include <linux/atomic.h> // атомарные операции
#include <linux/cdev.h>   // Символьные устройства cdev
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h> // Добавлено для virt_to_phys
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>         // для работы с poll
#include <linux/sched/signal.h> // Для send_sig_info, struct kernel_siginfo
#include <linux/signal.h>       // Для определения сигналов (SIGUSR1) и SI_QUEUE
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bogdan");
MODULE_DESCRIPTION("Driver for RESTful ipc");

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
static long ipc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    INF("=== new ioctl request ===");
    struct reg_task_t *reg_task = filp->private_data;
    struct server_t *server = NULL;
    struct client_t *client = NULL;
    struct connection_t *conn = NULL;
    struct serv_conn_list_t *scon = NULL;
    int sub_mem_id;
    int server_id;
    int ret = 0;

    // для регистрации сервера
    struct server_registration reg;

    // для подключения клиента к серверу
    struct connect_to_server con;

    // если нет описания структуры, то выходим
    if (!reg_task)
    {
        ERR("There is no reg_task in private_data");
        return -ENOENT;
    }

    // проверка типа и номеров битовых полей, чтобы не декодировать неверные команды
    if (_IOC_TYPE(cmd) != IOCTL_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > IOCTL_MAX_NUM)
        return -ENOTTY;

    // проверка команды
    switch (cmd)
    {
        // регистрация нового сервера
    case IOCTL_REGISTER_SERVER:

        INF("REGISTER_SERVER");

        // проверяем возможность регистрации сервера в процессе
        if (!reg_task_can_add_server(reg_task))
        {
            ERR("cannot add server to PID");
            return -ENOSPC;
        }

        // копируем имя из userspace
        if (copy_from_user(&reg, (void __user *)arg, sizeof(reg)))
        {
            ERR("copy_from_user failed\n");
            return -EFAULT;
        }

        // ищем сервер по имени
        server = find_server_by_name(reg.name);
        if (server)
        {
            ERR("server already exists: %d:%s", server->m_id, server->m_name);
            return -EEXIST;
        }

        // создаем сервер
        server = server_create(reg.name);

        reg.server_id = server->m_id;

        // отправляем id обратно в userspace
        if (copy_to_user((void __user *)arg, &reg, sizeof(reg)))
        {
            ERR("cant sand back server's id: %d:%s", server->m_id, server->m_name);
            return -EFAULT;
        }

        // регистрируем сервер в процессе
        reg_task_add_server(reg_task, server);

        if (!server->m_task_p)
        {
            ERR("Server's task ptr i NULL");
            ret = -ENOENT;
            break;
        }

        INF("New server is registered: (ID:%d) (NAME:%s) (PID:%d)", server->m_id, server->m_name,
            server->m_task_p->m_reg_task->m_task_p->pid);
        break;

        // регистрация нового клиента
    case IOCTL_REGISTER_CLIENT:

        INF("IOCTL_REGISTER_CLIENT");

        // проверяем возможность регистрации клиента в процессе
        if (!reg_task_can_add_client(reg_task))
        {
            ERR("cannot add client to PID");
            return -ENOSPC;
        }

        // создали новго клиента
        client = client_create();

        // отправляем id обратно в userspace
        if (copy_to_user((void __user *)arg, &client->m_id, sizeof(client->m_id)))
        {
            ERR("REGISTER_CLIENT: cant sand back clients's id: %d", client->m_id);
            return -EFAULT;
        }

        // регистрируем клиент в процессе
        reg_task_add_client(reg_task, client);

        if (!client->m_task_p)
        {
            ERR("client's task ptr is NULL");
            ret = -ENOENT;
            break;
        }

        INF("REGISTER_CLIENT: New client is registered: (ID:%d) (PID:%d)", client->m_id,
            client->m_task_p->m_reg_task->m_task_p->pid);
        break;

        // подключение клиента к серверу
    case IOCTL_CONNECT_TO_SERVER:

        INF("IOCTL_CONNECT_TO_SERVER");
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
            INF("CONNECT_TO_SERVER: client %d already connected to server %s", con.client_id, con.server_name);
            return -EEXIST;
        }

        // ищем сервер с подходящим именем
        server = find_server_by_name(con.server_name);

        // если не нашли сервер
        if (!server)
        {
            ERR("CONNECT_TO_SERVER: there is no server with name: %s", con.server_name);
            return -ENODATA;
        }

        // подключаем клиента к серверу
        ret = connect_client_to_server(server, client);

        if (ret != 0)
        {
            ERR("CONNECT_TO_SERVER: connect_client_to_server: %d", ret);
            return ret;
        }
        break;

    case IOCTL_CLIENT_END_WRITING:

        INF("IOCTL_CLIENT_END_WRITING");
        // Получение id клиента из аргумента
        int id = unpack_id1((u32)arg);

        // поиск нужного клиента
        client = find_client_by_id_pid(id, current->pid);

        // если клиент не найден
        if (!client)
        {
            ERR("There is no client with id %d", id);
            return -ENODATA;
        }

        conn = client->m_conn_p;

        // если есть клиент, но он не подключен
        if (!conn)
        {
            ERR("There is no connection in client (ID:%d)", id);
            return -ENOENT;
        }

        // получаем сервер
        server = conn->m_server_p;

        // Если нет указателя на сервер
        if (!server)
        {
            ERR("Ivalid connection object: null server ptr (CLIENT ID:%d)", client->m_id);
            return -ENOMEM;
        }

        // отправка уведомления
        if ((ret = notification_send(CLIENT, NEW_MESSAGE, conn)) != 0)
        {
            ERR("sending notif failed");
        }

        break;

    case IOCTL_SERVER_END_WRITING:

        INF("IOCTL_SERVER_END_WRITING");
        // Получение id сервера и памяти из аргумента
        UNPACK_SC_SHM((u32)arg, server_id, sub_mem_id);

        // поиск нужного сервера
        server = find_server_by_id(server_id);

        // если сервер не найден
        if (!server)
        {
            ERR("There is no server with id %d", id);
            return -ENODATA;
        }

        // поиск нужного соединения
        conn = server_find_conn_by_sub_mem_id(server, sub_mem_id)->conn;

        // если нет соединения с этой памятью
        if (!conn)
        {
            ERR("There is no connection btw server (ID:%d) and sub_mem (ID:%d)", server_id, sub_mem_id);
            return -ENOENT;
        }

        // получаем клиент
        client = conn->m_client_p;

        // Если нет указателя на клиент
        if (!client)
        {
            ERR("Ivalid connection object: null client ptr (SERVER ID:%d) (SUB MEM ID: %d)", server_id, sub_mem_id);
            return -ENOMEM;
        }

        if ((ret = notification_send(SERVER, NEW_MESSAGE, conn)) != 0)
        {
            ERR("sending notif failed");
        }
        break;

    case IOCTL_CLIENT_DISCONNECT:

        INF("IOCTL_CLIENT_DISCONNECT");
        // Получение id клиента из аргумента
        int client_id = unpack_id1((u32)arg);

        // поиск нужного клиента
        client = find_client_by_id(client_id);

        // проверка на получение клиента
        if (!client)
        {
            ERR("There is no client with id: %d", client_id);
            return -ENOENT;
        }

        // получение соединения
        conn = client->m_conn_p;

        if (!conn)
        {
            ERR("There is no connection in client (ID:%d)(PID:%d)", client_id, reg_task->m_task_p->pid);
            return -ENOENT;
        }

        // уведомляем сервер о разрыве соединения
        if ((ret = notification_send(CLIENT, REMOTE_DISCONNECT, conn)) != 0)
        {
            ERR("sending notif failed");
        }

        // отключаемся от сервера
        client_cleanup_connection(client);
        break;

    case IOCTL_SERVER_DISCONNECT:

        INF("IOCTL_SERVER_DISCONNECT");
        // Получение id сервера и памяти из аргумента
        UNPACK_SC_SHM((u32)arg, server_id, sub_mem_id);

        // поиск нужного сервера
        server = find_server_by_id(server_id);

        // если сервер не найден
        if (!server)
        {
            ERR("There is no server with id %d", id);
            return -ENODATA;
        }

        // поиск нужного соединения
        scon = server_find_conn_by_sub_mem_id(server, sub_mem_id);

        // если не нашлось такого серверного соединения
        if (!scon)
        {
            ERR("There is no server connection unit for connection btw server (ID:%d) and sub_mem (ID:%d)", server_id,
                sub_mem_id);
            return -ENOENT;
        }

        conn = scon->conn;

        // если нет соединения с этой памятью
        if (!conn)
        {
            ERR("There is no connection btw server (ID:%d) and sub_mem (ID:%d)", server_id, sub_mem_id);
            return -ENOENT;
        }

        // получаем клиент
        client = conn->m_client_p;

        // Если нет указателя на клиент
        if (!client)
        {
            ERR("Ivalid connection object: null client ptr (SERVER ID:%d) (SUB MEM ID: %d)", server_id, sub_mem_id);
            return -ENOMEM;
        }

        if ((ret = notification_send(SERVER, REMOTE_DISCONNECT, conn)) != 0)
        {
            ERR("sending notif failed");
        }

        // удаляем соединение
        server_cleanup_connection(server, scon);
        break;

    case IOCTL_CLIENT_UNREGISTER:

        INF("IOCTL_CLIENT_UNREGISTER");
        // Получение id клиента из аргумента
        client_id = unpack_id1((u32)arg);

        // поиск нужного клиента
        client = find_client_by_id(client_id);

        // проверка на получение клиента
        if (!client)
        {
            ERR("There is no client with id: %d", client_id);
            return -ENOENT;
        }

        // получение соединения
        conn = client->m_conn_p;

        if (!conn)
        {
            ERR("There is no connection in client (ID:%d)(PID:%d)", client_id, reg_task->m_task_p->pid);
            return -ENOENT;
        }

        // уведомляем сервер о разрыве соединения
        if ((ret = notification_send(CLIENT, REMOTE_DISCONNECT, conn)) != 0)
        {
            ERR("sending notif failed");
        }

        // --- удаляем клиента ---
        // отключаем его от сервера
        client_cleanup_connection(client);

        // удаляем сам клиент
        reg_task_delete_client(client->m_task_p);

        break;

    case IOCTL_SERVER_UNREGISTER:

        INF("IOCTL_SERVER_UNREGISTER");
        // Получение id сервера из аргумента
        server_id = unpack_id1((u32)arg);

        // поиск нужного сервера
        server = find_server_by_id(server_id);

        // если сервер не найден
        if (!server)
        {
            ERR("There is no server with id %d", server_id);
            return -ENODATA;
        }

        // удаляем соединения
        server_cleanup_connections(server);

        // очищаем память
        reg_task_delete_server(server->m_task_p);

        break;

    case IOCTL_REGISTER_MONITOR:

        INF("IOCTL_REGISTER_MONITOR");
        return reg_task_set_monitor(reg_task);

        break;

    default:
        INF("Unknown ioctl command: 0x%x", cmd);
        return -ENOTTY;
    }
    return ret;
}

/**
 * Обработчик mmap
 */
static int ipc_mmap(struct file *file, struct vm_area_struct *vma)
{
    INF("=== new mmap request ===");

    int ret = 0;
    u32 packed_id = (u32)vma->vm_pgoff;
    struct client_t *client = NULL;
    struct server_t *server = NULL;
    struct sub_mem_t *sub = NULL;
    struct connection_t *conn = NULL;
    struct serv_conn_list_t *srv_conn = NULL;
    u32 packed_cli_sub_id = 0;

    // id не может быть отрицательным
    if (packed_id < 0)
    {
        ERR("Incorrect id: %d", packed_id);
        return -EINVAL;
    }
    int target_id = unpack_id1(packed_id); // id клиента либо сервера
    int sub_id = unpack_id2(packed_id);    // id памяти (не всегда передается)

    INF("packed_id=0x%x (from vma->vm_pgoff), target_id=%d, sub_mem_id=%d", packed_id, target_id, sub_id);

    /**
     * Нужно найти зарегистрированного клиента или сервера,
     * который запросил отображение памяти
     */

    // ищем клиента
    client = find_client_by_id_pid(target_id, current->pid);

    // если current+id - это клиент
    if (client)
    {
        // сохраняем соединение, в котором нужная нам память
        conn = client->m_conn_p;

        // если соединения нет, то и соединять не с чем
        if (!conn)
        {
            ERR("There is no connection (CLIENT ID:%d)", client->m_id);
            return -EINVAL;
        }

        // запакованные client_id + shm_id
        packed_cli_sub_id = PACK_SC_SHM(client->m_id, conn->m_mem_p->m_id);

        // если произошла ошибка упаковки
        if (packed_cli_sub_id == (u32)-EINVAL)
        {
            // Ошибка упаковки (например, ID вышли за диапазон) - маловероятно, если ID генерируются правильно
            ERR("Failed to pack IDs for signal (client_id=%d, shm_id=%d)\n", client->m_id, conn->m_mem_p->m_id);
            // Не отправляем сигнал
            goto found;
        }

        INF("Data packed: (client_id=%d, sub_mem_id=%d)\n", client->m_id, conn->m_mem_p->m_id);

        // чтобы сервер не удалился или не изменился, пока сигнал отправляю
        mutex_lock(&conn->m_server_p->m_lock);

        // отправка уведомления
        if ((ret = notification_send(CLIENT, NEW_CONNECTION, conn)) != 0)
        {
            ERR("notification sending failed");
        }

        // устанавливаем флаг в значение: память отображена на сервере
        atomic_set(&conn->m_serv_mmaped, 1);
        mutex_unlock(&conn->m_server_p->m_lock);

        goto found;
    }
    client = NULL;

    // ищем сервер, если это был не клиент
    server = find_server_by_id(target_id);

    // если current+id - это сервер
    if (server)
    {
        // сохраняем соединение, в котором нужная нам память
        if (!list_empty(&server->connection_list.list))
        {
            // нужно знать shm_id для поиска памяти
            if (sub_id != 0)
            {
                // ищем нужную память
                srv_conn = server_find_conn_by_sub_mem_id(server, sub_id);

                // проверка на существование подключенной памяти с таким id
                if (!srv_conn)
                {
                    ERR("Server (ID: %d) (NAME: %s) does not have connection with (SHM MEM ID: %d)", server->m_id,
                        server->m_name, sub_id);
                    return -ENOENT;
                }
                conn = srv_conn->conn;
            }
            else
            {
                // Cервер не должен вызывать mmap с shm_id=0, потому что общая память
                // не может создаться до создания клиента или сервера, которые заберут начальные id.
                ERR("Server %d (pid %d) called mmap with sub_mem_id=0.", target_id, current->pid);
                return -EINVAL;
            }
        }
        else
        {
            INF("There is no connection to '%s' server", server->m_name);
            return -ENOENT;
        }
        goto found;
    }
    server = NULL;

    ERR("No client/server with ID %d found for PID %d\n", target_id, current->pid);
    return -ENOENT;

found:
    // Проверяем, что соединение и общая память существуют
    if (!conn || !conn->m_mem_p)
    {
        ERR("Connection or shared memory is NULL\n");
        return -EFAULT;
    }
    sub = conn->m_mem_p;

    // Отображаем физическую память в пользовательское пространство
    ret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(sub->m_pages_p), sub->m_size, vma->vm_page_prot);

    if (ret)
    {
        ERR("remap_pfn_range failed: %d\n", ret);
        return ret;
    }

    INF("PID %d mapped shm %p (size: %zu)\n", current->pid, sub, sub->m_size);
    return 0;
}

// обработчик подключения к драйверу
static int ipc_open(struct inode *inode, struct file *filp)
{
    INF("=== new open request ===");

    // проверка на возможность регистрации процесса
    if (!reg_task_can_add_task())
    {
        ERR("Not enough space for new task");
        return -ENOSPC;
    }

    struct reg_task_t *reg_task = reg_task_create();

    if (!reg_task)
    {
        ERR("Cant create reg_task");
        return -ENOMEM;
    }

    filp->private_data = reg_task;
    INF("Added private data");
    return 0;
}

// обработчик отключения от драйвера
static int ipc_release(struct inode *inode, struct file *filp)
{
    INF("=== new close request ===");
    struct reg_task_t *reg_task = filp->private_data;

    if (!reg_task)
    {
        ERR("There is no reg_task in private_data");
        return 0;
    }

    INF("Cleaning up task resources for PID %d", reg_task->m_task_p ? reg_task->m_task_p->pid : -1);

    // Вся логика очистки теперь в reg_task_delete
    reg_task_delete(reg_task);
    filp->private_data = NULL;

    INF("Release: Task cleanup complete.");
    return 0;
}

static ssize_t ipc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    INF("=== new read request ===");
    struct reg_task_t *reg_task = filp->private_data;

    if (!reg_task)
    {
        ERR("There is no reg_task in private_data");
        return -ENOENT;
    }
    size_t size = 0;

    // если это монитор, то нужно вернуть информацию о драйвере
    if (reg_task_is_monitor(reg_task))
    {
        INF("Request from monitor process");
        size = sizeof(struct st_reg_tasks);

        if (count < size)
        {
            ERR("Not enough space");
            return -EMSGSIZE;
        }

        struct st_reg_tasks *reg_tasks = kmalloc(size, GFP_KERNEL);
        reg_task_get_data(reg_tasks);

        // копируем данные в user space
        if (copy_to_user(buf, reg_tasks, size))
        {
            ERR("copy_to_user error");
            kfree(reg_tasks);
            return -EFAULT;
        }

        kfree(reg_tasks);
        return size;
    }

    // иначе обрабатываем получение уведомления
    INF("Request from general process");

    // печать размер очереди уведомлений
    int notif_count = reg_task_get_notif_count(reg_task);
    INF("(PID:%d) notif count: %d", reg_task->m_task_p->pid, notif_count);

    // структура уведомления
    struct notification_t *notif;

    // получаем уведомление
    notif = reg_task_get_notification(reg_task);

    // если получить не удалось
    if (!notif)
    {
        INF("There is no more notifications");
        return -ENOENT;
    }
    size = sizeof(notif->data);

    // сравниваем размер, получаемого сообщения
    if (size > count)
    {
        ERR("Not enough space");
        return -EMSGSIZE;
    }

    // копируем данные в user space
    if (copy_to_user(buf, &notif->data, size))
    {
        ERR("copy_to_user error");
        return -EFAULT;
    }

    // удаляем уведомление из очереди
    notification_delete(notif);

    return size;
}

static __poll_t ipc_poll(struct file *filp, poll_table *wait)
{
    // INF("=== new poll request ===");
    struct reg_task_t *reg_task = filp->private_data;

    if (!reg_task)
    {
        ERR("There is no reg_task in private_data");
        return -ENOENT;
    }

    __poll_t mask = 0;

    // блокируем процесс
    mutex_lock(&reg_task->m_wait_queue_lock);
    poll_wait(filp, &reg_task->m_wait_queue, wait);
    mutex_unlock(&reg_task->m_wait_queue_lock);

    int ret = reg_task_is_notif_pending(reg_task);

    // если есть уведомления, то говорим об этом
    if (ret == 1)
        mask |= EPOLLIN | EPOLLRDNORM;

    return mask;
}

// Операции файла драйвера
static struct file_operations g_fops = {
    .owner = THIS_MODULE,
    .open = ipc_open,
    .release = ipc_release,
    .unlocked_ioctl = ipc_ioctl,
    .mmap = ipc_mmap,
    .read = ipc_read,
    .poll = ipc_poll,
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
    INF("=== RIPC Driver loading ===");
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

    // Создание устройства /dev/ripc c правами (rw-rw-rw-)
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
    INF("=== RIPC Driver Unloading ===");

    // Удаление всех зарегистрированных задач (процессов)
    // Это должно каскадно вызвать очистку серверов, клиентов и соединений,
    // принадлежащих этим задачам, через ipc_release -> reg_task_delete.
    // Но на случай, если какие-то задачи не были корректно удалены
    // (например, процесс был убит -9 и release не вызвался),
    // пройдемся по списку задач и принудительно их удалим.
    struct reg_task_t *reg_task, *reg_tmp;
    INF("Cleaning up remaining registered tasks...");
    mutex_lock(&g_reg_task_lock);
    list_for_each_entry_safe(reg_task, reg_tmp, &g_reg_task_list, list)
    {
        // Удаляем из списка перед вызовом delete, чтобы избежать гонок,
        // так как reg_task_delete тоже лочит и удаляет.
        list_del(&reg_task->list);
        mutex_unlock(&g_reg_task_lock);

        INF("Force deleting reg_task for PID %d during exit.", reg_task->m_task_p ? reg_task->m_task_p->pid : -1);

        // Вызовет очистку серверов/клиентов/соединений
        reg_task_delete(reg_task);

        mutex_lock(&g_reg_task_lock);
    }
    mutex_unlock(&g_reg_task_lock);
    INF("Finished cleaning registered tasks.");

    // Дополнительная проверка и очистка глобальных списков
    INF("Performing final cleanup of global lists...");
    delete_server_list();
    delete_client_list();
    // Пулы памяти SHM (удалит shm_t и вызовет submem_clear)
    delete_shm_list();

    INF("Finished final list cleanup.");

    // Освобождение генератора ID
    INF("Destroying global ID generator...");
    DELETE_ID_GENERATOR(&g_id_gen);
    INF("ID generator destroyed.");

    // Удаление символьного устройства и класса
    INF("Removing character device and class...");
    if (g_cdev.ops)
    { // Проверяем, была ли инициализирована cdev
        cdev_del(&g_cdev);
        INF("Character device deleted.");
    }
    if (g_dev_class)
    {
        // Удаляем сам файл устройства (/dev/ripc)
        device_destroy(g_dev_class, g_dev_num);
        INF("Device node /dev/%s destroyed.", DEVICE_NAME);
        // Удаляем класс устройства
        class_destroy(g_dev_class);
        INF("Device class '%s' destroyed.", CLASS_NAME);
        g_dev_class = NULL;
    }

    // Освобождение диапазона номеров устройств
    if (g_major != 0)
    {
        unregister_chrdev_region(g_dev_num, g_dev_count);
        INF("Character device region (Major: %d) unregistered.", g_major);
    }

    INF("RIPC driver unloaded successfully.");
}

module_init(ipc_init);
module_exit(ipc_exit);