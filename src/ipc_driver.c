#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/poll.h>  // Добавлено для poll_wait, POLLIN, POLLRDNORM
#include <linux/io.h>    // Добавлено для virt_to_phys

#define DEVICE_NAME "ultrafast_ipc"
#define BUFFER_SIZE PAGE_SIZE
#define IOCTL_NOTIFY_SERVER _IOW('M', 1, int)
#define IOCTL_REGISTER_SERVER _IOW('M', 2, int)
#define IOCTL_NOTIFY_CLIENT _IOW('M', 3, int)

static char *shared_buffer;
static DECLARE_WAIT_QUEUE_HEAD(server_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(client_wait_queue);
static pid_t server_pid = -1;  // PID сервера
static pid_t client_pid = -1;  // PID клиента

//  Функция отправки сигнала процессу
static void notify_process(pid_t pid) {
    struct pid *pid_struct;
    struct task_struct *task;

    if (pid < 0) {
        printk(KERN_WARNING "IPC: No registered process!\n");
        return;
    }

    pid_struct = find_get_pid(pid);
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (task) {
        send_sig(SIGUSR1, task, 0);  // Отправляем сигнал SIGUSR1
    }
}

// Обработчик ioctl()
static long ipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    if (cmd == IOCTL_NOTIFY_SERVER) {
        wake_up_interruptible(&server_wait_queue);  // Будим сервер
        notify_process(server_pid);  // Отправляем сигнал серверу
    } else if (cmd == IOCTL_REGISTER_SERVER) {
        if (copy_from_user(&server_pid, (int __user *)arg, sizeof(int))) {
            return -EFAULT;
        }
        printk(KERN_INFO "IPC: Server registered with PID %d\n", server_pid);
    } else if (cmd == IOCTL_NOTIFY_CLIENT) {
        wake_up_interruptible(&client_wait_queue);  // Будим клиента
        notify_process(client_pid);  // Отправляем сигнал клиенту
    }
    return 0;
}

// poll() для ожидания данных
static __poll_t ipc_poll(struct file *file, struct poll_table_struct *wait) {
    poll_wait(file, &server_wait_queue, wait);
    return POLLIN | POLLRDNORM;
}

static int ipc_mmap(struct file *file, struct vm_area_struct *vma) {
    return remap_pfn_range(vma, vma->vm_start, virt_to_phys(shared_buffer) >> PAGE_SHIFT,
                           vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static const struct file_operations ipc_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = ipc_ioctl,
    .mmap = ipc_mmap,
    .poll = ipc_poll,
};

static struct miscdevice ipc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &ipc_fops,
};

static int __init ipc_init(void) {
    shared_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!shared_buffer) return -ENOMEM;
    return misc_register(&ipc_device);
}

static void __exit ipc_exit(void) {
    misc_deregister(&ipc_device);
    kfree(shared_buffer);
}

module_init(ipc_init);
module_exit(ipc_exit);
MODULE_LICENSE("GPL");