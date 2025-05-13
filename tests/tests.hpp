#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

// Простые макросы для assert
#define ASSERT(condition)\
    if (!(condition))                                                                                    \
    {                                                                                                    \
        std::cerr << "ASSERTION FAILED: " #condition " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1);                                                                                    \
    }

#define ASSERT_NO_THROW(statement)\
    try                                                                                                                 \
    {                                                                                                                   \
        statement;                                                                                                      \
    }                                                                                                                   \
    catch (const std::exception &e)                                                                                     \
    {                                                                                                                   \
        std::cerr << "ASSERTION FAILED: Expected no throw for " #statement ", but got: " << e.what() << std::endl;      \
        std::exit(1);                                                                                                   \
    }                                                                                                                   \
    catch (...)                                                                                                         \
    {                                                                                                                   \
        std::cerr << "ASSERTION FAILED: Expected no throw for " #statement ", but got unknown exception." << std::endl; \
        std::exit(1);                                                                                                   \
    }

#define ASSERT_THROWS(statement, ExceptionType)\
    try                                                                                                                                           \
    {                                                                                                                                             \
        statement;                                                                                                                                \
        std::cerr << "ASSERTION FAILED: Expected throw " #ExceptionType " for " #statement ", but no exception was thrown." << std::endl;         \
        std::exit(1);                                                                                                                             \
    }                                                                                                                                             \
    catch (const ExceptionType &)                                                                                                                 \
    {                                                                                                                                             \
        /* Expected exception */                                                                                                                  \
    }                                                                                                                                             \
    catch (const std::exception &e)                                                                                                               \
    {                                                                                                                                             \
        std::cerr << "ASSERTION FAILED: Expected throw " #ExceptionType " for " #statement ", but got std::exception: " << e.what() << std::endl; \
        std::exit(1);                                                                                                                             \
    }                                                                                                                                             \
    catch (...)                                                                                                                                   \
    {                                                                                                                                             \
        std::cerr << "ASSERTION FAILED: Expected throw " #ExceptionType " for " #statement ", but got unknown exception." << std::endl;           \
        std::exit(1);\                                                                                                                          
    }