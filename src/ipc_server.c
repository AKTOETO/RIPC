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
#define IOCTL_REGISTER_SERVER _IOW('M', 2, int)
#define IOCTL_NOTIFY_CLIENT _IOW('M', 3, int)

int fd;
char *shared_memory;

// Обработчик сигнала SIGUSR1
void signal_handler(int signo, siginfo_t *info, void *context) {
    if (signo == SIGUSR1) {
        printf("Server: Received SIGUSR1 from kernel!\n");
        write(1, shared_memory, BUFFER_SIZE);
    }
}

int main() {
    fd = open("/dev/ultrafast_ipc", O_RDWR | O_NONBLOCK);  // Открываем в неблокирующем режиме
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

    //  Устанавливаем обработчик сигнала SIGUSR1
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGUSR1, &sa, NULL);

    //  Передаем PID сервера в драйвер
    int pid = getpid();
    ioctl(fd, IOCTL_REGISTER_SERVER, &pid);
    printf("Server: Registered with PID %d\n", pid);

    struct pollfd pfd = { .fd = fd, .events = POLLIN };

    while (1) {
        printf("Server: Doing other work...\n");
        int ret = poll(&pfd, 1, 5000);  //  Ожидаем данные 5 секунд
        if (ret > 0 && (pfd.revents & POLLIN)) {
            printf("Server: Processing request: %s\n", shared_memory);
            sleep(1);

            //  Отправляем ответ клиенту
            strcpy(shared_memory, "Response from server!");
            ioctl(fd, IOCTL_NOTIFY_CLIENT, NULL);
            printf("Server: Response sent to client\n");
        } else if (ret == 0) {
            printf("Server: No requests, continuing other tasks...\n");
        } else {
            perror("poll error");
            break;
        }
    }

    close(fd);
    return 0;
}
