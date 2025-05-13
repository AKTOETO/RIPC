include(FetchContent)

# подключаем googletest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.14.0
  VERBATIM
)
FetchContent_MakeAvailable(googletest)
add_library(GTest::GTest INTERFACE IMPORTED)
target_link_libraries(GTest::GTest INTERFACE gtest_main)

# каталог функционального тестирования
add_subdirectory(functional)

# каталог нагрузочного тестировани
add_subdirectory(load)