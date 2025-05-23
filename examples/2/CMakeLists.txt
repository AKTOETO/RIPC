# CMake-файл для тестового приложения 
set(CLIENT client)
set(SERVER server)

# Определяем исполняемый файл и его исходник
add_executable(${CLIENT} client.cpp)
add_executable(${SERVER} server.cpp)

# Линкуем тестовое приложение с нашей библиотекой libripc
target_link_libraries(${CLIENT} PRIVATE ripc)
target_link_libraries(${SERVER} PRIVATE ripc)

# Добавляем флаги компиляции для теста
target_compile_options(${CLIENT} PRIVATE -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer -march=native) # Например, отладка
target_link_options(${CLIENT} PRIVATE -fsanitize=address -fno-omit-frame-pointer) 
target_compile_options(${SERVER} PRIVATE -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer -march=native) # Например, отладка
target_link_options(${SERVER} PRIVATE -fsanitize=address -fno-omit-frame-pointer) 

# --- Установка ---
# include(GNUInstallDirs)
# install(TARGETS test_app DESTINATION ${CMAKE_INSTALL_BINDIR})