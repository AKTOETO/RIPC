# Определение расположения исходников драйвера
DRIVER_DIR := driver

# Папка для сборки
BUILD_DIR := build

# Если KERNELRELEASE определен - сборка внутри ядра
ifneq ($(KERNELRELEASE),)
    obj-m += ipc_driver.o
# Иначе - сборка из командной строки
else
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    PWD := $(shell pwd)
endif


default: help

b:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)/$(DRIVER_DIR) modules

# Цель для установки модуля
i:
	sudo insmod $(DRIVER_DIR)/ipc_driver.ko
	@echo "Driver loaded. Check dmesg for logs."

# Цель для удаления модуля
r:
	sudo rmmod ipc_driver
	@echo "Driver unloaded."

# Цель для очистки
c:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)/$(DRIVER_DIR) clean
	rm -f $(DRIVER_DIR)/*.ko
	rm -f $(DRIVER_DIR)/*.mod.c
	rm -rf $(DRIVER_DIR)/.tmp_versions
	rm -f $(DRIVER_DIR)/Module.symvers
	rm -f $(DRIVER_DIR)/modules.order

# Дополнительные цели для удобства
rb: c b

# отображение логов
logs:
	sudo dmesg -w | grep 'RIPC:'

help:
	@echo "Available targets:"
	@echo "  b    - Build the kernel module"
	@echo "  i    - Insert the module (sudo required)"
	@echo "  r    - Remove the module (sudo required)"
	@echo "  c    - Clean build artifacts"
	@echo "  rb   - Clean and rebuild"
	@echo "  logs - Show driver's logs (sudo required)"
