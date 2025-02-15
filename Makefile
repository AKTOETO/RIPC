# Компилятор для пользовательских программ
CC = gcc-12

# Флаги компиляции для пользовательских программ
CFLAGS = -Wall -Wextra -I./lib

# Директории
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = $(BUILD_DIR)/bin
OBJ_DIR = $(BUILD_DIR)/obj

# Исходные файлы
SRC_CLIENT = $(SRC_DIR)/ipc_client.c
SRC_SERVER = $(SRC_DIR)/ipc_server.c
SRC_DRIVER = $(SRC_DIR)/ipc_driver.c

# Объектные файлы
OBJ_CLIENT = $(OBJ_DIR)/ipc_client.o
OBJ_SERVER = $(OBJ_DIR)/ipc_server.o

# Исполняемые файлы
CLIENT = $(BIN_DIR)/ipc_client
SERVER = $(BIN_DIR)/ipc_server
DRIVER_MODULE = $(BIN_DIR)/ipc_driver.ko

# Путь к заголовочным файлам ядра
KERNEL_HEADERS = /lib/modules/$(shell uname -r)/build

# Проверка наличия заголовочных файлов ядра
check_kernel_headers:
	@if [ ! -d "$(KERNEL_HEADERS)" ]; then \
		echo "Kernel headers not found. Please install them with: sudo apt install linux-headers-$(uname -r)"; \
		exit 1; \
	fi

# Цели по умолчанию
all: $(CLIENT) $(SERVER) $(DRIVER_MODULE)

# Создание необходимых директорий
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Компиляция клиента
$(CLIENT): $(OBJ_CLIENT) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJ_CLIENT): $(SRC_CLIENT) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Компиляция сервера
$(SERVER): $(OBJ_SERVER) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJ_SERVER): $(SRC_SERVER) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Компиляция драйвера (модуля ядра)
$(DRIVER_MODULE): check_kernel_headers $(SRC_DRIVER) | $(BIN_DIR)
	$(MAKE) -C $(KERNEL_HEADERS) M=$(PWD)/$(SRC_DIR) CC=gcc-12 modules
	@cp $(SRC_DIR)/ipc_driver.ko $(BIN_DIR)/

# Очистка
clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(KERNEL_HEADERS) M=$(PWD)/$(SRC_DIR) clean

# Пересборка
rebuild: clean all

# Загрузка драйвера
load_driver: $(DRIVER_MODULE)
	@if [ "$(shell id -u)" -ne 0 ]; then \
		echo "You must be root to load the driver. Use sudo."; \
		exit 1; \
	fi
	sudo insmod $(DRIVER_MODULE)

.PHONY: all clean rebuild check_kernel_headers load_driver