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
#include <linux/io.h>     // Добавлено для virt_to_phys
#include <linux/cdev.h>   // Символьные устройства cdev
#include <linux/printk.h> // Для printk
#include <asm/ioctl.h>    // Для доп проверок в ioctl

// Устанавливаем префикс для journalctl -t RIPC
#undef pr_fmt
#define pr_fmt(fmt) "RIPC: %s:%d: " fmt, __FILE__, __LINE__
#define INF(fmt, ...) pr_info(fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...) pr_err(fmt "\n", ##__VA_ARGS__)

#define CLASS_NAME "ripc"         // имя класса устройств
#define DEVICE_NAME "ripc"        // Имя устройства в /dev
#define MAX_SERVER_NAME 64        // Максимальная длина имени сервера
#define SHARED_MEM_SIZE PAGE_SIZE // Размер общей памяти (1 страница)

// Переменные для регистрации устройства
static int g_major = 0;           // номер устройства (major)
static int g_minor = 0;           // номер устройства (minor)
static int g_dev_count = 1;       // количество устройств
static dev_t g_dev_num;           // Номер устройства (major+minor)
static struct class *g_dev_class; // Класс устройства
static struct cdev g_cdev;        // Структура символьного устройства

/*
 *  IOCTL commands
 */
#define IOCTL_MAGIC '/'
#define IOCTL_REGISTER_SERVER _IOW(IOCTL_MAGIC, 1, char *) // регистрация сервера в системе
#define IOCTL_MAX_NUM 1

/*
 *  Список серверов
 */
struct server
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
struct client
{
    int m_id;                    // id клиента в процессе
    struct server *m_server_ptr; // указатель на сервер, к которому подключен
    struct task_struct *m_task;  // указатель на задачу, где зарегистрирован сервер
    struct shm *m_shm_ptr;       // указатель на общую память
    struct list_head list;       // список клиентов
};

/*
 * Общая память
 */
struct shm
{
    void *m_ptr;           // указатеь на область общей памяти
    size_t m_size;         // размер общей памяти
    int m_is_writing;      // флаг: пишет ли кто-то в памят или нет
    struct list_head list; // список областей памяти
};

// регистрируем списки
static LIST_HEAD(g_server_list);
static LIST_HEAD(g_client_list);
static DEFINE_MUTEX(g_lock); // глобальная блокировка

// Обработчик ioctl()
static long ipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int t_err = 0, t_ret = 0;
    char t_name[MAX_SERVER_NAME];
    struct server_info *t_server;
    struct client_info *t_client;
    struct shared_memory *t_shm;

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
        if(copy_from_user(t_name, (char __user *)arg, MAX_SERVER_NAME))
        {
            ERR("REGISTER_SERVER: copy_from_user failed\n");
            return -EFAULT;
        }

        break;

    default:
        INF("Unknown ioctl command: %d", cmd);
        break;
    }

    return (long)0;
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

    // Создание устройства /dev/ripc_ipc
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

    // TODO: создание структур серверов, клиентов, общей памяти

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
    // TODO: очистка структур сервера, памяти, клиентов

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