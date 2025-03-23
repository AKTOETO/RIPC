# Корневой Makefile
DRIVER_DIR = driver
SERVER_DIR = server
CLIENT_DIR = client

# Компиляторы
KERNEL_CC = gcc-12  # Компилятор для драйвера
APP_CC = gcc        # Компилятор для приложений


default: help

a: d s c

d:
	$(MAKE) -C $(DRIVER_DIR) CC=$(KERNEL_CC) b

di: 
	$(MAKE) -C $(DRIVER_DIR) CC=$(KERNEL_CC) i

dr:
	$(MAKE) -C $(DRIVER_DIR) CC=$(KERNEL_CC) r

s:
	$(MAKE) -C $(SERVER_DIR) CC=$(APP_CC) b

c:
	$(MAKE) -C $(CLIENT_DIR) CC=$(APP_CC) b

cl:
	$(MAKE) -C $(DRIVER_DIR) CC=$(APP_CC) c
	$(MAKE) -C $(SERVER_DIR) CC=$(APP_CC) c
	$(MAKE) -C $(CLIENT_DIR) CC=$(APP_CC) c

help:
	@echo "Available targets:"
	@echo "  d  - Driver's makefile"
	@echo "  s  - Server's makefile"
	@echo "  c  - Client's makefile"
	@echo "  cl - Clean all targets"
	@echo "  di - Install driver"
	@echo "  dr - Remove driver"