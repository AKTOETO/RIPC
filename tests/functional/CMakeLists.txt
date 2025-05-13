# tests/functional/CMakeLists.txt

# Находим все исходные файлы тестов (.cpp) в текущем каталоге
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Для каждого тестового файла создаем исполняемый файл
foreach(TEST_SOURCE_FILE ${TEST_SOURCES})
    message(STATUS "Adding test file ${TEST_SOURCE_FILE}")
    
    # Извлекаем имя файла без расширения
    get_filename_component(TEST_NAME ${TEST_SOURCE_FILE} NAME_WE) # NAME_WE = Name Without Extension   

    message(STATUS "Filename: ${TEST_NAME}")
    
    # Создаем исполняемый файл для теста. Имя цели теперь корректное.
    add_executable(${TEST_NAME} ${TEST_SOURCE_FILE})
    
    # Линкуем тест с вашей библиотекой libripc
    # Это также добавит необходимые include пути от libripc
    target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest ripc)
    
    # Опционально: если тестам нужны специфичные флаги или библиотеки
    # target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -g)
    # target_link_libraries(${TEST_NAME} PRIVATE SomeOtherTestLibrary)
    # Добавляем тест в систему CTest
    # Имя теста в CTest может быть более описательным
    #add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
    add_test(NAME Functional_${TEST_NAME} COMMAND ${TEST_NAME})

    # Опционально: установить рабочую директорию для теста, если нужно
    # Обычно тесты запускаются из каталога сборки, где находится исполняемый файл
    # set_tests_properties(Functional_${TEST_NAME} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()