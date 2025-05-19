# CMakeLists.txt для утилиты ripcctl

# Имя исполняемого файла
set(UTILITY_NAME ripcctl)

# Исходный файл
set(UTILITY_SOURCES ripcctl.c) # Или ваше имя файла

# Создаем исполняемый файл
add_executable(${UTILITY_NAME} ${UTILITY_SOURCES})

# Указываем пути к заголовочным файлам
# ${CMAKE_SOURCE_DIR} - это корневой каталог вашего проекта, где лежит главный CMakeLists.txt
# Предполагается, что ripc.h и id_pack.h лежат в <корень_проекта>/include/
target_include_directories(${UTILITY_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Если утилита зависит от вашей библиотеки libripc (что маловероятно для такой простой утилиты,
# но возможно для более сложной), то нужно было бы добавить:
# target_link_libraries(${UTILITY_NAME} PRIVATE ripc)
# Но для данного кода это не требуется, так как он использует только системные вызовы и ripc.h.

# Опционально: флаги компиляции
target_compile_options(${UTILITY_NAME} PRIVATE -Wall -Wextra -g)

# Опционально: установка утилиты
# include(GNUInstallDirs)
# install(TARGETS ${UTILITY_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})