# CMake-файл для библиотеки libripc

include(FetchContent)

# Подключаем nlohmann/json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # укажи нужную версию (или latest)
)
FetchContent_MakeAvailable(json)


# Находим все исходные файлы (.cpp) в каталоге src/
file(GLOB LIB_SOURCES src/*.cpp)

# Находим все заголовочные файлы (.h) в каталоге include/ripc/
# Это нужно для IDE и правильных зависимостей при изменениях
file(GLOB LIB_HEADERS include/ripc/*.hpp)

# Создаем статическую библиотеку с именем 'ripc'
# STATIC - библиотека будет вкомпилирована в конечный исполняемый файл
add_library(ripc STATIC ${LIB_SOURCES} ${LIB_HEADERS})

# Определяем пути для заголовочных файлов:
target_include_directories(ripc
    PUBLIC
        # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>:
        #   Делает каталог libripc/include доступным для других целей (например, test_app)
        #   во время сборки. Пользователь будет включать как #include <ripc/ripc.h>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

        # $<INSTALL_INTERFACE:include>:
        #   Определяет путь для include при установке библиотеки (если будет).
        #   Пользователь установленной библиотеки будет включать как #include <ripc/ripc.h>
        $<INSTALL_INTERFACE:include>

        # ${CMAKE_SOURCE_DIR}/include:
        #   Добавляет корневой каталог include/ (где лежат ripc.h/id_pack.h от драйвера)
        #   как для самой библиотеки, так и для ее потребителей.
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        # ${CMAKE_CURRENT_SOURCE_DIR}/src:
        #   Каталог src/ доступен только внутри самой библиотеки (если нужны внутренние includes)
        #   В данном случае может быть не нужен, но для порядка оставим.
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Указываем зависимости библиотеки:
# RipcEntityManager использует std::thread, поэтому нужна pthread.
target_link_libraries(ripc PRIVATE pthread nlohmann_json::nlohmann_json)
target_link_libraries(ripc PUBLIC nlohmann_json::nlohmann_json)

# Добавляем специфичные флаги компиляции для библиотеки
# target_compile_options(ripc PRIVATE -O2)

# --- Установка библиотеки ---
# include(GNUInstallDirs)
# install(TARGETS ripc
#         EXPORT ripcTargets
#         ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#         LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )
# # Устанавливаем публичные заголовки в include/ripc/
# install(DIRECTORY include/ripc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
#
# install(EXPORT ripcTargets
#         FILE ripcTargets.cmake
#         NAMESPACE ripc::
#         DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ripc
# )