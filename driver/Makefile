CC ?= gcc-12  # По умолчанию компилятор

obj-m += ipc_driver.o

# Путь к ядру
KERNELDIR ?= /lib/modules/$(shell uname -r)/build

# Добавляем каталог include
ccflags-y += -I$(CURDIR)/../include

default: help

# Сборка
b:
	@echo ""
	@echo "== BUILDING DRIVER =="
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) CC=$(CC) modules

# очистка верменных файлов
c:
	@echo ""
	@echo "== CLEANING DRIVER =="
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) CC=$(CC) clean
	rm -rf *.ko *.o *.mod.c .*.cmd .tmp_versions Module.symvers modules.order

# установка модуля ядра
i:
	sudo insmod ipc_driver.ko

# извлечение модуля ядра
r:
	sudo rmmod ipc_driver

# персборка
cb: c b

# отображение логов
l:
	sudo dmesg -w | grep 'RIPC:'

# помощь
help: 
	@echo ""
	@echo "== DRIVER's HELP =="
	@echo "  b  - Build the kernel module"
	@echo "  c  - Clean build artifacts"
	@echo "  i  - Insert the module (sudo required)"
	@echo "  l  - Show driver's logs (sudo required)"
	@echo "  r  - Remove the module (sudo required)"
	@echo "  cb - Clean and rebuild"


