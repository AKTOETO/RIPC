#include "ripc/logger.hpp"
#include <chrono>
#include <cstring>
#include <ctime>
#include <thread>
#include <vector>

namespace ripc
{

    Logger::Logger()
        : m_output_stream(&std::cerr),                           // По умолчанию пишем в stderr
          m_current_level(LogLevel::INFO),                       // Уровень по умолчанию
          m_critical_behavior(CriticalBehavior::THROW_EXCEPTION) // По умолчанию бросаем
    {
    }

    Logger &Logger::getInstance()
    {
        static Logger instance; // Meyers' Singleton
        return instance;
    }

    void Logger::setLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_level = level;
    }

    LogLevel Logger::getLevel() const
    {
        return m_current_level;
    }

    void Logger::setOutputStream(std::ostream *os)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output_stream = (os ? os : &std::cerr); // Если nullptr, сбрасываем на stderr
    }

    void Logger::setCriticalBehavior(CriticalBehavior behavior)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_critical_behavior = behavior;
    }

    Logger::CriticalBehavior Logger::getCriticalBehavior() const
    {
        // std::lock_guard<std::mutex> lock(m_mutex); // Опционально
        return m_critical_behavior;
    }

    const char *Logger::levelToString(LogLevel level) const
    {
        switch (level)
        {
        case LogLevel::CRITICAL:
            return "CRIT";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::WARNING:
            return "WARN";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::TRACE:
            return "TRACE";
        default:
            return "UNKN";
        }
    }

    std::string Logger::formatPrefix(LogLevel level, const char *file, int line, const char *function)
    {
        std::ostringstream prefix;
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);

        // Форматируем время: ГГГГ-ММ-ДД ЧЧ:ММ:СС.мс
        // std::put_time не выводит миллисекунды стандартно, используем chrono
        std::tm tm_snapshot;
        localtime_r(&now_c, &tm_snapshot); // Потокобезопасный localtime

        auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());

        prefix << "[" << levelToString(level) << "] " << std::put_time(&tm_snapshot, "%Y-%m-%d %H:%M:%S") << '.'
               << std::setw(3) << std::setfill('0') << ms.count() << " ";

        // пишем thread id
        prefix << "[TID: " << std::this_thread::get_id() << "]";

        // Убираем полный путь к файлу, оставляем только имя файла
        const char *last_slash = strrchr(file, '/');
        const char *last_backslash = strrchr(file, '\\');
        const char *filename_only = file;
        if (last_slash)
            filename_only = last_slash + 1;
        if (last_backslash && last_backslash + 1 > filename_only)
            filename_only = last_backslash + 1;

        prefix << "[" << filename_only << ":" << line << " " << function << "] ";
        return prefix.str();
    }

    void Logger::log(LogLevel level, const char *file, int line, const char *function, const char *format, ...)
    {
        // Проверяем уровень логгирования ДО захвата мьютекса для производительности
        if (static_cast<int>(level) > static_cast<int>(m_current_level) || level == LogLevel::NONE)
        {
            return;
        }

        std::string message_str;
        // Форматируем сообщение в стиле printf
        // Нужно быть осторожным с размером буфера, если сообщения могут быть очень длинными
        std::vector<char> buffer(1024); // Начальный размер буфера
        va_list args1, args2;
        va_start(args1, format);
        va_copy(args2, args1); // Копируем va_list, так как vsnprintf может его изменить

        // Первая попытка с буфером начального размера
        int needed = vsnprintf(buffer.data(), buffer.size(), format, args1);
        va_end(args1);

        if (needed < 0)
        { // Ошибка форматирования
            message_str = "Error formatting log message.";
        }
        else if (static_cast<size_t>(needed) >= buffer.size())
        {
            // Буфер был слишком мал, повторяем с нужным размером
            buffer.resize(static_cast<size_t>(needed) + 1); // +1 для нуль-терминатора
            vsnprintf(buffer.data(), buffer.size(), format, args2);
            message_str = buffer.data();
        }
        else
        {
            message_str = buffer.data();
        }
        va_end(args2);

        // Критическое поведение и вывод под мьютексом
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Повторная проверка уровня на случай, если он изменился между первой проверкой и захватом мьютекса
            if (static_cast<int>(level) > static_cast<int>(m_current_level) || level == LogLevel::NONE)
            {
                return;
            }

            if (m_output_stream)
            {
                (*m_output_stream) << formatPrefix(level, file, line, function) << message_str << std::endl;
            }

            if (level == LogLevel::CRITICAL && m_critical_behavior == CriticalBehavior::THROW_EXCEPTION)
            {
                // Формируем сообщение для исключения, включая префикс
                std::string exception_message = formatPrefix(level, file, line, function) + message_str;
                throw CriticalLogError(exception_message);
            }
        }
    }

} // namespace ripc