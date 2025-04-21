#CC ?= gcc-12  # По умолчанию компилятор

# итоговый модуль
obj-m += dripc.o

# объектные файлы для сборки модуля
dripc-y := id.o connection.o client.o server.o shm.o main.o sigs.o

# Добавляем каталог include
#EXTRA_CFLAGS += -I$(CURDIR)/../include
ccflags-y += -I$(PWD)/include 
ccflags-y += -I$(PWD)/driver 
#ccflags-y += -I$(CURDIR)/.../include

# Путь к ядру
KERNELDIR ?= /lib/modules/$(shell uname -r)/build

default: help

# Сборка
b:
	@echo ""
	@echo "== BUILDING DRIVER =="
	@echo "Компиляция с флагами: $(ccflags-y)"
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) modules

# очистка верменных файлов
c:
	@echo ""
	@echo "== CLEANING DRIVER =="
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) clean
	rm -rf *.ko *.o *.mod.c .*.cmd .tmp_versions Module.symvers modules.order

# установка модуля ядра
i:
	sudo insmod dripc.ko

# извлечение модуля ядра
r:
	sudo rmmod dripc

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


