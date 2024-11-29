cmake_minimum_required(VERSION 3.23)
project(RIPC)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Условие для добавления определения DEBUG
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

# ищем нужные библиотеки
find_package(Boost REQUIRED)

# объявляем исходные файлы для библиотеки
file(GLOB RIPC_SRC "src/*.cpp")
file(GLOB RIPC_HDRS "include/*.h")
add_library(RIPC_lib STATIC 
	${RIPC_SRC}
	${RIPC_HDRS}
)

target_include_directories(RIPC_lib PUBLIC include)
target_link_libraries(RIPC_lib PUBLIC boost::boost)

# добавляем папку с тестами
add_subdirectory(test)