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
#include <linux/atomic.h>    // атомарные операции
#include <linux/io.h>        // Добавлено для virt_to_phys
#include <linux/cdev.h>      // Символьные устройства cdev
#include <linux/printk.h>    // Для printk
#include <asm/ioctl.h>       // Для доп проверок в ioctl
#include "../include/ripc.h" // константы для драйвера

// Устанавливаем префикс для journalctl -t RIPC
#undef pr_fmt
#define pr_fmt(fmt) "RIPC: %s:%d: " fmt, __FILE__, __LINE__
#define INF(fmt, ...) pr_info(fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...) pr_err(fmt "\n", ##__VA_ARGS__)

// Переменные для регистрации устройства
static int g_major = 0;           // номер устройства (major)
static int g_minor = 0;           // номер устройства (minor)
static int g_dev_count = 1;       // количество устройств
static dev_t g_dev_num;           // Номер устройства (major+minor)
static struct class *g_dev_class; // Класс устройства
static struct cdev g_cdev;        // Структура символьного устройства

/*
 *  Сервер
 */
struct server_t
{
    char m_name[MAX_SERVER_NAME]; // имя сервера
    int m_id;                     // id сервера в процессе
    struct list_head m_clients;   // список подключенных клиентов
    struct task_struct *m_task;   // указатель на задачу, где зарегистрирован сервер
    struct list_head list;        // список серверв
};

/*
 * Клиент
 */
struct client_t
{
    int m_id;                      // id клиента в процессе
    struct server_t *m_server_ptr; // указатель на сервер, к которому подключен
    struct task_struct *m_task;    // указатель на задачу, где зарегистрирован сервер
    struct shm_t *m_shm_ptr;       // указатель на общую память
    struct list_head list;         // список клиентов
};

/*
 * Общая память
 */
struct shm_t
{
    void *m_ptr;           // указатеь на область общей памяти
    size_t m_size;         // размер общей памяти
    atomic_t m_is_writing; // флаг: пишет ли кто-то в памят или нет
    struct list_head list; // список областей памяти
};

// регистрируем списки
static LIST_HEAD(g_server_list);
static LIST_HEAD(g_client_list);
static LIST_HEAD(g_shm_list);
static DEFINE_MUTEX(g_lock); // глобальная блокировка
static DEFINE_IDA(g_ida);    // Глобальный генератор id. TODO: нужно сделать свой под каждый процесс

// генерация id
static int generate_id(void)
{
    int id = ida_alloc(&g_ida, GFP_KERNEL);
    if (id < 0)
    {
        ERR("generate_id: Cannot allocate id");
    }
    return id;
}

// удаление id
static void free_id(int id)
{
    ida_free(&g_ida, id);
}

/**
 * Клиентские операции
 */
// создание клиента
static struct client_t *client_create(void)
{
    struct client_t *cli = kmalloc(sizeof(*cli), GFP_KERNEL);
    if (!cli)
    {
        ERR("client_create: Cant allocate memory for client");
        return NULL;
    }

    // Инициализация полей
    cli->m_id = generate_id();
    cli->m_server_ptr = NULL;
    cli->m_task = current;
    cli->m_shm_ptr = NULL;

    mutex_lock(&g_lock);
    list_add_tail(&cli->list, &g_client_list);
    mutex_unlock(&g_lock);

    INF("Client %d created", cli->m_id);

    return cli;
}

// удаление клиента
static void client_destroy(struct client_t *cli)
{
    // проверка входных данных
    if (!cli)
    {
        ERR("client_destroy: Attempt to destroy NULL client");
        return;
    }

    INF("Destroying client %d\n", cli->m_id);

    // удаление из глобального списка
    mutex_lock(&g_lock);
    list_del(&cli->list);
    mutex_unlock(&g_lock);

    free_id(cli->m_id);
    kfree(cli);
}

// поиск клиента по id
static struct client_t *find_client_by_id(int id)
{
    // проверка входных данных
    if (id < 0)
    {
        ERR("find_client_by_id: Incorrect id: %d", id);
        return NULL;
    }

    // проходимся по каждому клиенту и ищем подходящего

    struct client_t *client = NULL;

    // Итерируемся по списку клиентов
    list_for_each_entry(client, &g_client_list, list)
    {
        if (client->m_id == id)
        {
            // Нашли совпадение - сохраняем результат
            goto found_client_by_id;
        }
    }

    // Клиент не найден
    client = NULL;

found_client_by_id:
    return client;
}

/**
 * Серверные операции
 */

// поиск сервера по имени
static struct server_t *find_server_by_name(const char *name)
{
    struct server_t *srv;
    list_for_each_entry(srv, &g_server_list, list)
    {
        if (strcmp(srv->m_name, name) == 0)
        {
            return srv;
        }
    }
    return NULL;
}

// поиск клиента из спсика сервера по task_struct
static struct client_t *find_client_by_task_from_server(
    struct task_struct *task, struct server_t *serv)
{
    // проверяем входные данные
    if (!serv || !task)
    {
        ERR("Invalid input params");
        return NULL;
    }

    struct client_t *client = NULL;

    // Итерируемся по списку клиентов
    list_for_each_entry(client, &serv->m_clients, list)
    {
        if (client->m_task == task)
        {
            // Нашли совпадение - сохраняем результат
            goto found_client_by_task_from_server;
        }
    }

    // Клиент не найден
    client = NULL;

found_client_by_task_from_server:
    return client;
};

// добавление клиента к серверу
static int add_client_to_server(struct server_t *srv, struct client_t *cli)
{
    // Проверка входных параметров
    if (!srv || !cli)
    {
        ERR("add_client_to_server: Invalid arguments: srv=%p, cli=%p\n", srv, cli);
        return -EINVAL;
    }

    // Проверка, что клиент ещё не привязан
    if (cli->m_server_ptr)
    {
        ERR("add_client_to_server: Client %d already connected to server '%s'\n",
            cli->m_id, cli->m_server_ptr->m_name);
        return -EALREADY;
    }

    // Блокируем доступ к списку клиентов сервера
    mutex_lock(&g_lock);

    // Добавляем клиента в конец списка
    list_add_tail(&cli->list, &srv->m_clients);

    // Устанавливаем обратную ссылку
    cli->m_server_ptr = srv;

    // Разблокируем доступ
    mutex_unlock(&g_lock);

    INF("Client %d added to server '%s'\n", cli->m_id, srv->m_name);
    return 0;
}

// создание сервера
static struct server_t *server_create(const char *name)
{
    // проверка входных данные
    if (!name || strlen(name) == 0)
    {
        ERR("server_create: Invalid server name");
        return NULL;
    }

    // выделние и проверка памяти
    struct server_t *srv = kmalloc(sizeof(*srv), GFP_KERNEL);
    if (!srv)
    {
        ERR("server_create: Cant allocate memory for server");
        return NULL;
    }

    // Инициализация полей
    strscpy(srv->m_name, name, MAX_SERVER_NAME);
    srv->m_id = generate_id();
    INIT_LIST_HEAD(&srv->m_clients);
    srv->m_task = current;

    // добавление в главный список
    mutex_lock(&g_lock);
    list_add_tail(&srv->list, &g_server_list);
    mutex_unlock(&g_lock);

    INF("Server '%s' (ID: %d) created", srv->m_name, srv->m_id);

    return srv;
}

// удаление сервера
static void server_destroy(struct server_t *srv)
{
    // проверка входного параметра
    if (!srv)
    {
        ERR("server_destroy: Attempt to destroy NULL server");
        return;
    }

    INF(" Destroying server '%s' (ID: %d)", srv->m_name, srv->m_id);

    struct client_t *cli, *tmp;

    mutex_lock(&g_lock);

    // Удаление списка клиентов
    list_for_each_entry_safe(cli, tmp, &srv->m_clients, list)
    {
        list_del(&cli->list);
    }

    // удаление сервера из глобального списка
    list_del(&srv->list);
    mutex_unlock(&g_lock);

    free_id(srv->m_id);
    kfree(srv);
}

/**
 * Операции с общей памятью
 */

// создание общей памяти
static struct shm_t *shm_create(size_t size)
{
    // проверка входных параметров
    if (size == 0 || size > MAX_SHARED_MEM_SIZE)
    {
        ERR("shm_create: Invalid shared memory size: %ld", size);
        return NULL;
    }

    // выделяем память под структуру
    struct shm_t *shm = kmalloc(sizeof(*shm), GFP_KERNEL);
    if (!shm)
    {
        ERR("shm_create: Cant allocate shared memory struct");
        return NULL;
    }

    // выделякм память под саму область
    shm->m_ptr = kmalloc(size, GFP_KERNEL);
    if (!shm->m_ptr)
    {
        ERR("shm_create: Cant allocate shared memory area");
        kfree(shm);
        return NULL;
    }

    // инициализация полей
    shm->m_size = size;
    atomic_set(&shm->m_is_writing, 0);
    INIT_LIST_HEAD(&shm->list);

    INF("Shared memory allocated (%zu bytes)", shm->m_size);

    return shm;
}

// удаление общей памяти
static void shm_destroy(struct shm_t *shm)
{
    // проверка входных параметров
    if (!shm)
    {
        ERR("shm_destroy: Attempt to destroy NULL shared memory");
        return;
    }

    INF("Destroying shared memory (%zu bytes)", shm->m_size);

    // освобождение память под общую область
    if (shm->m_ptr)
        kfree(shm->m_ptr);

    // освобождение памяти под структуру
    kfree(shm);
}

// начать запись в память
static int shm_start_write(struct shm_t *shm)
{
    // проверка входных данных
    if (!shm)
    {
        ERR("shm_start_write: Attempt to write to NULL shared memory");
        return -1;
    }

    // Пытаемся изменить 0 → 1 атомарно
    if (atomic_cmpxchg(&shm->m_is_writing, 0, 1) == 0)
    {
        return 0; // Успешно захватили флаг
    }
    return -EBUSY; // Кто-то уже пишет
}

// закончить запись в память
static void shm_end_write(struct shm_t *shm)
{
    // проверка входных данных
    if (!shm)
    {
        ERR("shm_end_write: Attempt to end writing to NULL shared memory");
        return;
    }

    // Гарантированно сбрасываем флаг
    atomic_set(&shm->m_is_writing, 0);
}

/**
 * Обработчик ioctl()
 */
static long ipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    struct server_t *server = NULL;
    struct client_t *client = NULL;
    struct client_t *client2 = NULL;
    struct shm_t *shm = NULL;

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
        if (copy_from_user(&reg, (char __user *)arg, sizeof(reg)))
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
        if (copy_from_user(&con, (char __user *)arg, sizeof(con)))
        {
            ERR("CONNECT_TO_SERVER: copy_from_user failed\n");
            return -EFAULT;
        }

        // ищем клиента с con.id
        client = find_client_by_id(con.client_id);

        // если подключение с нужным сервером существует, выходим
        if (client->m_server_ptr &&
            strncmp(client->m_server_ptr->m_name, con.server_name, MAX_SERVER_NAME) == 0)
        {
            INF("CONNECT_TO_SERVER: client %d already connected to server %s",
                con.client_id, con.server_name);
            return -EEXIST;
        }

        // ищем сервер с таким именем
        server = find_server_by_name(con.server_name);

        // если не нашли сервер
        if (!server)
        {
            ERR("CONNECT_TO_SERVER: there is no server with name: %s", con.server_name);
            return -ENODATA;
        }

        // сервер найден, проверяем уже существующие подключения с процессом этого клиента
        client2 = find_client_by_task_from_server(client->m_task, server);

        // если существует клиент из того же процесса, с которого пытается подключиться
        // еще один клиент, то просто даем еще одному клиенту ту же область памяти
        if (client2)
            client->m_shm_ptr = client2->m_shm_ptr;

        // если же нет общей памяти у сервера с процессом,
        // из которого к нему подключается клиент, то создаем ее
        else
        {
            shm = shm_create(SHARED_MEM_SIZE);

            // проверка создания памяти
            if (!shm)
            {
                ERR("CONNECT_TO_SERVER: cant create shared memory for client %d and server %s",
                    client->m_id, server->m_name);
                return -ENOMEM;
            }

            // подключаем память к клиенту
            client->m_shm_ptr = shm;
        }

        // подключаем клиента к серверу
        if ((ret = add_client_to_server(server, client)))
        {
            ERR("CONNECT_TO_SERVER: failed to connect client %d to server %s",
                client->m_id, server->m_name);
            shm_destroy(shm);
            return ret;
        }

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
    struct server_t *server, *server_tmp;
    struct client_t *client, *client_tmp;
    struct shm_t *shm, *shm_tmp;

    // Очистка списка клиентов
    list_for_each_entry_safe(client, client_tmp, &g_client_list, list)
        client_destroy(client);

    // Очистка списка серверов
    list_for_each_entry_safe(server, server_tmp, &g_server_list, list)
        server_destroy(server);

    // Очистка списка памятей
    list_for_each_entry_safe(shm, shm_tmp, &g_shm_list, list)
        shm_destroy(shm);

    // удаление счетчика
    ida_destroy(&g_ida);

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