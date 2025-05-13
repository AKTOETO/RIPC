# CMake-файл для тестового приложения test_app

# Определяем исполняемый файл и его исходник
add_executable(test_app app.cpp)

# Линкуем тестовое приложение с нашей библиотекой libripc
target_link_libraries(test_app PRIVATE ripc)

# Добавляем флаги компиляции для теста
target_compile_options(test_app PRIVATE -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer) # Например, отладка
target_link_options(test_app PRIVATE -fsanitize=address -fno-omit-frame-pointer) 

# --- Установка ---
# include(GNUInstallDirs)
# install(TARGETS test_app DESTINATION ${CMAKE_INSTALL_BINDIR})