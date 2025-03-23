#include "ripc.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>

int main() {
    int fd = open("/dev/" DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char server_name[MAX_SERVER_NAME] = "my_server";
    if (ioctl(fd, IOCTL_REGISTER_SERVER, server_name) < 0) {
        perror("ioctl REGISTER_SERVER");
        close(fd);
        return 1;
    }

    printf("Server '%s' registered successfully\n", server_name);
    close(fd);
    return 0;
}