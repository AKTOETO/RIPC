#ifndef SIGS_H
#define SIGS_H

#include <linux/sched/signal.h>       // send_sig_info
#include <linux/pid.h>                // find_get_pid, pid_task, put_pid
#include <uapi/asm-generic/siginfo.h> // SI_KERNEL / SI_QUEUE
#include <linux/rcupdate.h>           // rcu_read_lock/unlock
#include <linux/sched/task.h>         // Для get_task_struct/put_task_struct (если нужно)

// --- Функция отправки сигнала в драйвере ---
// @param target_pid PID процесса-получателя
// @param signal_num Номер сигнала (40, 41 и т.д.)
// @param value1 Первое число для передачи (в si_int)
// @param value2 Второе число для передачи (в si_errno)
int send_signal_to_process(pid_t target_pid, int signal_num, int value1, int value2);

#endif // !SIGS_H