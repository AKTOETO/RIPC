#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched/signal.h> // Для send_sig_info, struct kernel_siginfo
#include <linux/list.h>
#include <linux/string.h>
#include <linux/signal.h> // Для определения сигналов (SIGUSR1) и SI_KERNEL
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/atomic.h>    // атомарные операции
#include <linux/io.h>        // Добавлено для virt_to_phys
#include <asm/pgtable.h>     // макросы для работы с таблицей страниц
#include <linux/cdev.h>      // Символьные устройства cdev
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
    int ret = 0;
    struct server_t *server = NULL;
    struct client_t *client = NULL;
    // struct client_t *client2 = NULL;
    // struct shm_t *shm = NULL;

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

        INF("REGISTER_SERVER: New server is registered: (ID:%d) (NAME:%s) (PID:%d)", server->m_id, server->m_name, server->m_task_p->pid);
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
        ret = connect_client_to_server(server, client);

        if(ret != 0)
        {
            ERR("CONNECT_TO_SERVER: connect_client_to_server: %d", ret);
            return ret;
        }
        break;

    default:
        INF("Unknown ioctl command: 0x%x", cmd);
        return -ENOTTY;
    }
    return 0;
}

/**
 * Обработчик mmap
 */
static int ipc_mmap(struct file *file, struct vm_area_struct *vma)
{
    int ret = 0;
    // Получаем упакованный ID из смещения в СТРАНИЦАХ ---
    // vma->vm_pgoff содержит (offset_из_userspace / PAGE_SIZE),
    // что как раз и есть наше исходное packed_id.
    u32 packed_id = (u32)vma->vm_pgoff;
    struct client_t *client = NULL;
    struct server_t *server = NULL;
    struct shm_t *shm = NULL;
    struct connection_t *conn = NULL;
    u32 packed_cli_shm_id = 0;
    struct kernel_siginfo sig_info; // для отправки сигнала

    // TODO: пока еще нет ограничений на id, в будущем переделать
    // Проверяем корректность offset (должен содержать id клиента/сервера)
    // if (offset < MIN_ID || offset > MAX_ID) {
    //     pr_err("Invalid offset (expected ID in range [%d-%d])\n", MIN_ID, MAX_ID);
    //     return -EINVAL;
    // }
    // id не может быть отрицательным
    if (packed_id < 0)
    {
        ERR("Incorrect id: %d", packed_id);
        return -EINVAL;
    }
    int target_id = unpack_id1(packed_id); // id клиента либо сервера
    int shm_id = unpack_id2(packed_id);    // id памяти (не всегда передается)

    INF("packed_id=0x%x (from vma->vm_pgoff), target_id=%d, shm_id=%d",
        packed_id, target_id, shm_id);

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

        // TODO: отправляем сигнал на сервер, что нужно отобразить память
        // TODO: передать надо client_id + shm_id
        // conn->m_server_p->m_task_p

        // запакованные client_id + shm_id
        packed_cli_shm_id = PACK_SC_SHM(client->m_id, conn->m_mem_p->m_id);

        // если произошла ошибка упаковки
        if (packed_cli_shm_id == (u32)-EINVAL)
        {
            // Ошибка упаковки (например, ID вышли за диапазон) - маловероятно, если ID генерируются правильно
            ERR("Failed to pack IDs for signal (client_id=%d, shm_id=%d)\n", client->m_id, conn->m_mem_p->m_id);
            // Не отправляем сигнал
            goto found;
        }

        INF("Data packed: (client_id=%d, shm_id=%d)\n", client->m_id, conn->m_mem_p->m_id);

        // Подготавливаем структуру kernel_siginfo
        memset(&sig_info, 0, sizeof(struct kernel_siginfo));
        sig_info.si_signo = NEW_CONNECTION; // Номер сигнала (ваш SIGUSR1)
        sig_info.si_code = SI_KERNEL;       // Указываем, что сигнал из ядра
        // Помещаем упакованные данные в поле si_int.
        // Сервер должен будет извлечь их из siginfo_t->si_int в своем обработчике.
        sig_info.si_int = (int)packed_cli_shm_id;

        // чтобы сервер не удалился или не изменился, пока сигнал отправляю
        mutex_lock(&conn->m_server_p->m_lock);

        INF("Sending signal %d to server PID %d with data 0x%x (client=%d, shm=%d)\n",
            NEW_CONNECTION, conn->m_server_p->m_task_p->pid, packed_cli_shm_id, client->m_id, conn->m_mem_p->m_id);
        int sig_ret = send_sig_info(NEW_CONNECTION, &sig_info, conn->m_server_p->m_task_p);

        // устанавливаем флаг в значение: память отображена на сервере
        atomic_set(&conn->m_serv_mmaped, 1);
        mutex_unlock(&conn->m_server_p->m_lock);

        // если возникла ошибка при отправке сигнала
        if (sig_ret < 0)
        {
            ERR("Failed to send signal %d to server PID %d: error %d\n",
                NEW_CONNECTION, conn->m_server_p->m_task_p->pid, sig_ret);
            // Ошибка отправки (например, процесс уже не существует - ESRCH).
            // Обычно не критично для mmap, просто логируем.
        }
        else
        {
            INF("Signal %d sent successfully to server PID %d\n", NEW_CONNECTION, conn->m_server_p->m_task_p->pid);
        }

        goto found;
    }
    client = NULL;

    // ищем сервер, если это был не клиент
    server = find_server_by_id_pid(target_id, current->pid);

    // если current+id - это сервер
    if (server)
    {
        // сохраняем соединение, в котором нужная нам память
        if (!list_empty(&server->connection_list))
        {
            // нужно знать shm_id для поиска памяти
            if (shm_id != 0)
            {
                // ищем нужную память
                conn = server_find_conn_by_id(server, shm_id);
            }
            else
            {
                // Cервер не должен вызывать mmap с shm_id=0, потому что общая память 
                // не может создаться до создания клиента или сервера, которые заберут начальные id.
                ERR("Server %d (pid %d) called mmap with shm_id=0.", target_id, current->pid);
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
    shm = conn->m_mem_p;

    // TODO: добавить проверку прав доступа мб
    // // Проверяем права доступа (процесс должен быть владельцем)
    // if (shm->m_owner_pid != current->pid)
    // {
    //     ERR("Process %d has no access to shm %p\n", current->pid, shm);
    //     return -EACCES;
    // }

    // Отображаем физическую память в пользовательское пространство
    ret = remap_pfn_range(vma,
                          vma->vm_start,
                          virt_to_phys(shm->m_mem_p) >> PAGE_SHIFT,
                          shm->m_size,
                          vma->vm_page_prot);
    if (ret)
    {
        ERR("remap_pfn_range failed: %d\n", ret);
        return ret;
    }

    // Увеличиваем счетчик подключений клиентов к памяти
    if (client)
    {
        atomic_inc(&shm->m_num_of_conn);
    }

    INF("PID %d mapped shm %p (size: %zu)\n", current->pid, shm, shm->m_size);

    // return remap_pfn_range(vma, vma->vm_start, virt_to_phys(shared_buffer) >> PAGE_SHIFT,
    //                        vma->vm_end - vma->vm_start, vma->vm_page_prot);

    return 0;
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

    // удаление глобального генераора id
    DELETE_ID_GENERATOR(&g_id_gen);

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