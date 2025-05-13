#ifndef RIPC_LOGGER_HPP
#define RIPC_LOGGER_HPP

#include <string>
#include <iostream>  // std::ostream, std::cerr
#include <fstream>   // std::ofstream
#include <sstream>   // std::ostringstream
#include <iomanip>   // std::put_time, std::setw, std::setfill
#include <chrono>    // std::chrono::system_clock
#include <mutex>     // std::mutex, std::lock_guard
#include <stdexcept> // std::runtime_error
#include <cstdio>    // vsnprintf, va_list (для printf-like форматирования)
#include <cstdarg>   // va_start, va_end

namespace ripc
{

    // Уровни логгирования
    enum class LogLevel
    {
        NONE = 0,     // Логгирование отключено
        CRITICAL = 1, // Критические ошибки, приложение скорее всего не может продолжать
        ERROR = 2,    // Ошибки, которые могут не приводить к падению
        WARNING = 3,  // Предупреждения
        INFO = 4,     // Информационные сообщения
        DEBUG = 5,    // Отладочные сообщения
        TRACE = 6     // Наиболее подробные сообщения
    };

    // Кастомное исключение для критических ошибок
    class CriticalLogError : public std::runtime_error
    {
    public:
        explicit CriticalLogError(const std::string &message) : std::runtime_error(message) {}
    };

    class Logger
    {
    public:
        // Запрещаем копирование и присваивание
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        // Получение экземпляра синглтона
        static Logger &getInstance();

        // Установка минимального уровня для вывода логов
        void setLevel(LogLevel level);
        LogLevel getLevel() const;

        // Установка потока вывода (по умолчанию std::cerr)
        // Передача nullptr сбрасывает на std::cerr
        void setOutputStream(std::ostream *os);

        // Управление поведением критических ошибок
        enum class CriticalBehavior
        {
            THROW_EXCEPTION, // Бросать исключение (по умолчанию)
            LOG_ONLY         // Только логгировать, не бросать
        };
        void setCriticalBehavior(CriticalBehavior behavior);
        CriticalBehavior getCriticalBehavior() const;

        // Основная функция логгирования (внутренняя, вызывается через макросы)
        void log(LogLevel level, const char *file, int line, const char *function, const char *format, ...);

    private:
        Logger();            // Приватный конструктор
        ~Logger() = default; // Деструктор по умолчанию

        std::ostream *m_output_stream;
        LogLevel m_current_level;
        CriticalBehavior m_critical_behavior;
        std::mutex m_mutex; // Для потокобезопасности вывода

        // Вспомогательная функция для форматирования префикса
        std::string formatPrefix(LogLevel level, const char *file, int line, const char *function);
        const char *levelToString(LogLevel level) const;
    };

    // --- Макросы для удобного вызова логгера ---
    // Используем __VA_ARGS__ для поддержки переменного числа аргументов в стиле printf

#define RIPC_LOG_MSG(level, format_str, ...)                                                                 \
    {                                                                                                        \
        if (static_cast<int>(level) <= static_cast<int>(ripc::Logger::getInstance().getLevel()) &&           \
            level != ripc::LogLevel::NONE)                                                                   \
        {                                                                                                    \
            ripc::Logger::getInstance().log(level, __FILE__, __LINE__, __func__, format_str, ##__VA_ARGS__); \
        }                                                                                                    \
    }

#define LOG_CRIT(format_str, ...) RIPC_LOG_MSG(ripc::LogLevel::CRITICAL, format_str, ##__VA_ARGS__)
#define LOG_ERR(format_str, ...) RIPC_LOG_MSG(ripc::LogLevel::ERROR, format_str, ##__VA_ARGS__)
#define LOG_WARN(format_str, ...) RIPC_LOG_MSG(ripc::LogLevel::WARNING, format_str, ##__VA_ARGS__)
#define LOG_INFO(format_str, ...) RIPC_LOG_MSG(ripc::LogLevel::INFO, format_str, ##__VA_ARGS__)
    // #define DEBUG(format_str, ...) RIPC_LOG_MSG(ripc::LogLevel::DEBUG, format_str, ##__VA_ARGS__)
    // #define TRACE(format_str, ...) RIPC_LOG_MSG(ripc::LogLevel::TRACE, format_str, ##__VA_ARGS__)

} // namespace ripc

#endif // RIPC_LOGGER_HPP