# CMake-файл для тестового приложения test_app
set(NAME simple_test)

# Определяем исполняемый файл и его исходник
add_executable(${NAME} app.cpp)

# Линкуем тестовое приложение с нашей библиотекой libripc
target_link_libraries(${NAME} PRIVATE ripc)

# Добавляем флаги компиляции для теста
target_compile_options(${NAME} PRIVATE -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer) # Например, отладка
target_link_options(${NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer) 

# --- Установка ---
# include(GNUInstallDirs)
# install(TARGETS test_app DESTINATION ${CMAKE_INSTALL_BINDIR})