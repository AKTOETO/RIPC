cmake_minimum_required(VERSION 4.0)

project(RIPC_Project LANGUAGES CXX C)

# Устанавливаем стандарт C++ 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Отключаем расширения GNU

# Включаем тестирование
# Обязательно для использования CTest
enable_testing()
include(CTest)

# --- Включение Подкаталогов ---
# драйвер
#add_subdirectory(driver)
# утилита мониторинга ripcctl
add_subdirectory(ripcctl)
# user-space библиотека
add_subdirectory(libripc)
# примеры
add_subdirectory(examples)
# Тесты
add_subdirectory(tests)

# --- Пользовательские цели верхнего уровня (для удобства) ---
# Они будут работать, так как цели из подкаталогов видны на верхнем уровне
add_custom_target(build_driver
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ripc_driver_kmod # Используем имя цели из driver/CMakeLists.txt
    COMMENT "Building the kernel module via its target"
    VERBATIM
)
add_custom_target(clean_driver
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ripc_driver_clean # Используем имя цели из driver/CMakeLists.txt
    COMMENT "Cleaning the kernel module via its target"
    VERBATIM
)

message(STATUS "Настройка проекта завершена. Для сборки всего: 'make' или 'cmake --build .'")
message(STATUS "Для сборки только драйвера: 'make ripc_driver_kmod' или 'make build_driver'")
message(STATUS "Для очистки только драйвера: 'make ripc_driver_clean' или 'make clean_driver'")
