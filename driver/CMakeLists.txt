# CMake-файл для сборки модуля ядра в каталоге driver

# --- Определяем переменные для сборки ядра ---

# Релиз ядра
execute_process(
    COMMAND uname -r
    OUTPUT_VARIABLE KERNEL_RELEASE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE UNAME_RESULT
    ERROR_QUIET
)
if(NOT UNAME_RESULT EQUAL 0 OR NOT KERNEL_RELEASE)
    message(FATAL_ERROR "[Driver] Не удалось определить релиз ядра.")
endif()
# message(STATUS "[Driver] Kernel release: ${KERNEL_RELEASE}") # Можно убрать для чистоты

# Каталог сборки ядра
set(KERNEL_BUILD_DIR "/lib/modules/${KERNEL_RELEASE}/build")
if(NOT EXISTS "${KERNEL_BUILD_DIR}")
    message(FATAL_ERROR "[Driver] Каталог сборки ядра не найден: ${KERNEL_BUILD_DIR}. Установите linux-headers-${KERNEL_RELEASE}.")
endif()
# message(STATUS "[Driver] Kernel build directory: ${KERNEL_BUILD_DIR}") # Можно убрать

# Текущий каталог исходников драйвера (M=...)
set(DRIVER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Определяем компилятор C (CMake обычно делает это сам)
# Можно раскомментировать, если Kbuild требует явного указания
# find_program(KERNEL_CC NAMES ${CMAKE_C_COMPILER} gcc clang)
# if(NOT KERNEL_CC)
#     message(WARNING "[Driver] Не удалось определить компилятор C, Kbuild будет использовать свой.")
#     set(CC_ENV "") # Не передавать CC=
# else()
#     set(CC_ENV "CC=${KERNEL_CC}") # Передаем CC=...
# endif()
# set(CC_ENV "CC=${CMAKE_C_COMPILER}") # Простой вариант - использовать тот же, что CMake

# --- Пользовательские Цели для Сборки и Очистки ---

# Цель для сборки модуля (`make modules`)
add_custom_target(ripc_driver_kmod ALL # ALL - собирать по умолчанию
    # Команда: запустить make в KERNEL_BUILD_DIR с M=DRIVER_SOURCE_DIR
    COMMAND ${CMAKE_COMMAND} -E env  $(MAKE) modules
    # Указываем рабочий каталог (где лежит Makefile драйвера)
    WORKING_DIRECTORY ${DRIVER_SOURCE_DIR}
    # Указываем ожидаемый результат (файл .ko в текущем каталоге)
    BYPRODUCTS "${DRIVER_SOURCE_DIR}/dripc.ko"
    # Комментарий при сборке
    COMMENT "[Driver] Building kernel module (${KERNEL_RELEASE})"
    # Передавать аргументы команды без изменений
    VERBATIM
    # Опционально: Зависимости, если нужно собрать что-то еще до драйвера
    # DEPENDS some_other_target
)

# Цель для очистки модуля (`make clean`)
add_custom_target(ripc_driver_clean
    COMMAND ${CMAKE_COMMAND} -E env  $(MAKE) clean
    WORKING_DIRECTORY ${DRIVER_SOURCE_DIR}
    COMMENT "[Driver] Cleaning kernel module"
    VERBATIM
)

# --- Опционально: Цели для установки/удаления модуля ---
# Эти цели НЕ будут выполняться по умолчанию (нет ALL)
# Они требуют прав sudo и должны вызываться явно: make install_driver / make remove_driver

# Цель для `insmod`
add_custom_target(install_driver
    # Зависит от сборки модуля
    DEPENDS ripc_driver_kmod
    # Команда установки (требует sudo)
    COMMAND sudo insmod "${DRIVER_SOURCE_DIR}/dripc.ko"
    WORKING_DIRECTORY ${DRIVER_SOURCE_DIR}
    COMMENT "[Driver] Inserting kernel module (sudo required)"
    VERBATIM
    USES_TERMINAL # Может потребоваться для ввода пароля sudo
)

# Цель для `rmmod`
add_custom_target(remove_driver
    # Команда удаления (требует sudo)
    COMMAND sudo rmmod dripc || true # || true - чтобы make не выдавал ошибку, если модуль не загружен
    WORKING_DIRECTORY ${DRIVER_SOURCE_DIR}
    COMMENT "[Driver] Removing kernel module (sudo required)"
    VERBATIM
    USES_TERMINAL
)

# Цель для перезагрузки (сначала удаляем, потом вставляем)
add_custom_target(reload_driver
    DEPENDS remove_driver install_driver # Выполнит remove, затем install
    COMMENT "[Driver] Reloading kernel module (sudo required)"
)