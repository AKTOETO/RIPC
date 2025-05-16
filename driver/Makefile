# Имя модуля (без .o)
MODULE_NAME := dripc
# Собираемый объект модуля
obj-m += $(MODULE_NAME).o

# Список объектных файлов (.o), из которых собирается модуль
$(MODULE_NAME)-y := id.o connection.o task.o client.o server.o shm.o main.o

# --- Флаги компиляции ---
# Добавляем пути include относительно каталога исходников модуля ($(src))
# и относительно корневого каталога проекта (нужно передать его как-то)
# Проще всего использовать абсолютные пути или пути относительно $(src)

# $(src) - переменная Kbuild, указывающая на каталог, переданный в M=...
# $(PWD) в данном контексте может быть непредсказуем при вызове из CMake

# Путь к корневому include (ripc.h, id_pack.h) - получаем через переменную окружения или опцию make
RIPC_ROOT_INCLUDE ?= $(abspath $(src)/../include)

# получение нужного компилятора
CURRENT := $(shell uname -r)
KERNELDIR ?= /lib/modules/$(CURRENT)/build
#CC := $(shell grep CONFIG_CC_VERSION_TEXT /boot/config-$(CURRENT) | head -n 1 | cut -d'"' -f2 | awk '{print $$1}')
CC := gcc

# Добавляем пути:
# 1. Корневой include
# 2. Текущий каталог исходников драйвера ($(src) == driver/)
ccflags-y += -I$(RIPC_ROOT_INCLUDE)
#ccflags-y += -I$(src)
ccflags-y += -I$(PWD)


# --- Цели Makefile (для вызова из CMake или вручную) ---

# Цель по умолчанию (обычно не используется при вызове извне)
# Но Kbuild требует цель 'modules'
all: modules

# Стандартные цели Kbuild
modules modules_install clean help:
	@echo FLAGS: $(ccflags-y)
	$(MAKE) CC=$(CC) -C $(KERNELDIR) M=$(CURDIR) modules EXTRA_CFLAGS="$(ccflags-y)"

.PHONY: all modules modules_install clean help

# Оставляем старые цели для ручного вызова из driver/
# (они не будут использоваться CMake)

b:
	@echo "[Driver Makefile] Building (manual target 'b')..."
	@echo FLAGS: $(ccflags-y)
	$(MAKE) CC=$(CC) -C $(KERNELDIR) M=$(CURDIR) modules EXTRA_CFLAGS="$(ccflags-y)"

c:
	@echo "[Driver Makefile] Cleaning (manual target 'c')..."
	$(MAKE) CC=$(CC) -C $(KERNELDIR) M=$(CURDIR) clean
	rm -f *.ko *.o *.mod.c .*.cmd .tmp_versions Module.symvers modules.order *.order

i: $(MODULE_NAME).ko
	sudo insmod $<

r:
	sudo rmmod $(MODULE_NAME) || true

cb: c b

l:
	sudo dmesg -w | grep 'RIPC:'

help_manual:
	@echo "[Driver Makefile] Manual targets:"
	@echo "  make b  - Build module"
	@echo "  make c  - Clean module"
	@echo "  make i  - Insert module (sudo)"
	@echo "  make r  - Remove module (sudo)"
	@echo "  make cb - Clean and rebuild"
	@echo "  make l  - Show kernel logs (sudo)"
