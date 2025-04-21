#include "sigs.h"
#include "err.h"

// --- Функция отправки сигнала в драйвере ---
// @param target_pid PID процесса-получателя
// @param signal_num Номер сигнала (40, 41 и т.д.)
// @param value1 Первое число для передачи (в si_int)
// @param value2 Второе число для передачи (в si_errno)
int send_signal_to_process(pid_t target_pid, int signal_num, int value1, int value2)
{
    struct kernel_siginfo info;
    struct pid *pid_struct = NULL;
    struct task_struct *task = NULL; // Указатель на целевую задачу
    int ret = -ESRCH;                // По умолчанию - процесс не найден

    if (target_pid <= 0)
    {
        ERR("Invalid target PID: %d\n", target_pid);
        return -EINVAL;
    }

    // Подготовка структуры siginfo (как раньше)
    memset(&info, 0, sizeof(struct kernel_siginfo));
    info.si_signo = signal_num;
    info.si_code = SI_KERNEL;
    info.si_int = value1;
    info.si_errno = value2;
    rcu_read_lock(); // current_uid() требует RCU lock
    info.si_uid = current_uid().val;
    rcu_read_unlock();
    info.si_pid = 0; // Отправитель - ядро

    INF("Attempting to send signal %d to PID %d (val1=%d, val2=%d)\n",
            signal_num, target_pid, value1, value2);

    // Поиск процесса и отправка сигнала (под RCU lock)
    rcu_read_lock();

    // Находим struct pid по числовому PID
    pid_struct = find_get_pid(target_pid);
    if (!pid_struct)
    {
        ERR("PID %d not found (find_get_pid failed).\n", target_pid);
        ret = -ESRCH;
        goto out_unlock; // Переходим к разблокировке RCU
    }

    // Получаем task_struct из struct pid
    // pid_task увеличивает счетчик ссылок на task_struct, если он найден!
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (!task)
    {
        ERR("Task not found for PID %d (pid_task failed, process likely exiting).\n", target_pid);
        ret = -ESRCH;
        // put_pid(pid_struct); // Уменьшаем счетчик pid_struct, так как pid_task не удался? Да.
        goto out_put_pid; // Переходим к освобождению pid_struct и разблокировке RCU
    }

    // Теперь у нас есть валидный указатель на task_struct (task)
    // и счетчик ссылок на него увеличен.

    // Отправляем сигнал, используя task_struct
    ret = send_sig_info(signal_num, &info, task);

    if (ret < 0)
    {
        ERR("send_sig_info to PID %d (task %p) failed with error %d\n",
               target_pid, task, ret);
    }
    else
    {
        INF("Signal %d sent successfully to PID %d (task %p)\n",
                signal_num, target_pid, task);
        // send_sig_info возвращает 0 при успехе
    }

    // Освобождаем task_struct (уменьшаем счетчик ссылок)
    put_task_struct(task); // !!! Обязательно после использования task !!!

out_put_pid:
    // Освобождаем pid_struct (уменьшаем счетчик ссылок)
    if (pid_struct)
    {
        put_pid(pid_struct); // !!! Обязательно после использования pid_struct !!!
    }
out_unlock:
    rcu_read_unlock(); // Снимаем RCU lock

    return ret; // Возвращаем результат send_sig_info или -ESRCH
}