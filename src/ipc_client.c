#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define IOCTL_NOTIFY_SERVER _IOW('M', 1, int)
#define IOCTL_NOTIFY_CLIENT _IOW('M', 3, int)

int fd;
char *shared_memory;

// 📌 Обработчик сигнала SIGUSR1 (клиент реагирует на ответ от сервера)
void signal_handler(int signo, siginfo_t *info, void *context) {
    if (signo == SIGUSR1) {
        printf("Client: Received SIGUSR1 from kernel!\n");
        printf("Client: Received response: %s\n", shared_memory);
    }
}

int main() {
    fd = open("/dev/ultrafast_ipc", O_RDWR);
    if (fd == -1) {
        perror("Failed to open IPC device");
        return 1;
    }

    shared_memory = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared_memory == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // 📌 Устанавливаем обработчик сигнала SIGUSR1
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGUSR1, &sa, NULL);

    // 📌 Отправляем запрос серверу
    strcpy(shared_memory, "Hello from client!");
    printf("Client: Request sent to server\n");
    ioctl(fd, IOCTL_NOTIFY_SERVER, NULL);

    // 📌 Ожидание ответа от драйвера через `poll()`
    struct pollfd pfd = { .fd = fd, .events = POLLIN };

    printf("Client: Waiting for response (non-blocking)...\n");
    while (1) {
        int ret = poll(&pfd, 1, 5000);  // Ожидаем макс. 5 сек
        if (ret > 0 && (pfd.revents & POLLIN)) {
            //ioctl(fd, IOCTL_NOTIFY_CLIENT, NULL);  // Подтверждаем получение
            break;
        } else if (ret == 0) {
            printf("Client: No response yet, doing other work...\n");
        } else {
            perror("poll error");
            break;
        }
    }

    close(fd);
    return 0;
}
