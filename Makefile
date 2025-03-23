# Корневой Makefile
DRIVER_DIR = driver
TEST_DIR = test

# Компиляторы
KERNEL_CC = gcc-12  # Компилятор для драйвера
APP_CC = gcc        # Компилятор для приложений


default: help

a: d t

d:
	$(MAKE) -C $(DRIVER_DIR) CC=$(KERNEL_CC) b

di: 
	$(MAKE) -C $(DRIVER_DIR) CC=$(KERNEL_CC) i

dr:
	$(MAKE) -C $(DRIVER_DIR) CC=$(KERNEL_CC) r

t:
	$(MAKE) -C $(TEST_DIR) CC=$(APP_CC) b

cl:
	$(MAKE) -C $(DRIVER_DIR) CC=$(APP_CC) c
	$(MAKE) -C $(TEST_DIR) CC=$(APP_CC) c

l: 
	sudo dmesg -w | grep 'RIPC:'


help:
	@echo "Available targets:"
	@echo "  d  - Driver's makefile"
	@echo "  s  - Test's makefile"
	@echo "  cl - Clean all targets"
	@echo "  di - Install driver"
	@echo "  dr - Remove driver"
	@echo "  l - Show logs"