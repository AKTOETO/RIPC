# CMake-файл для тестового приложения test_app
set(NAME test_app3)

set(SNAME ${NAME}_server)
set(CNAME ${NAME}_client)

# Определяем исполняемый файл и его исходник
add_executable(${SNAME} server.cpp)
add_executable(${CNAME} client.cpp)

# Линкуем тестовое приложение с нашей библиотекой libripc
target_link_libraries(${SNAME} PRIVATE ripc)
target_link_libraries(${CNAME} PRIVATE ripc)

# Добавляем флаги компиляции для теста
target_compile_options(${SNAME} PRIVATE -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer -march=native) # Например, отладка
target_link_options(${SNAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer) 
target_compile_options(${CNAME} PRIVATE -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer -march=native) # Например, отладка
target_link_options(${CNAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer) 