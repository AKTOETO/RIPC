include(FetchContent)

# подключаем googletest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)

# каталог функционального тестирования
add_subdirectory(functional)

# каталог нагрузочного тестировани
add_subdirectory(load)