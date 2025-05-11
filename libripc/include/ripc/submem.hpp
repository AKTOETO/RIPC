#ifndef RIPC_SUB_MEM_HPP
#define RIPC_SUB_MEM_HPP

#include "types.hpp"
#include "context.hpp"
#include <string>
#include <iostream>
#include <string>
#include <array>
#include <string_view>
#include <optional>

// структура, описывающая область памяти для общения
namespace ripc
{
    class Server;
    class Client;

    // структура общей области памяти
    struct Memory
    {
        // даем доступ к рабюоте собщей памятию клиенту и серверу
        friend class Server;
        friend class Client;

        Memory(RipcContext &context);

        Memory() = delete;
        ~Memory();

        // Запрет копирования
        Memory(const Memory &) = delete;
        Memory &operator=(const Memory &) = delete;

        RipcContext &m_context;
        // адрес памяти
        char *m_addr;
        // максимальный размер памяти
        size_t m_max_size;
        // отображена ли память
        bool m_is_mapped;

        void mmap(int first_id, int second_id);
        void unmap();

        // элемент после последнего
        char* end() const;

        // поиск
        char* find(size_t offset, char ch);
        char* find(size_t offset, const char* ch, size_t len);
        char* find(size_t offset, const std::string& chars);

        // чтение
        size_t readUntil(size_t offset, char *buffer, size_t buffer_size, char delim);
        size_t read(size_t offset, char *buffer, size_t buffer_size);
        std::string read(size_t offset = 0, size_t buffer_size = 0);

        // запись
        size_t write(size_t offset, const char *buffer, size_t buffer_size);
        size_t write(size_t offset, std::string data);
        bool add(size_t offset, char ch);
    };

    // Буфер для регулирования доступа к общей памяти
    class BufferView
    {
    protected:

        // Адрес памяти, которой управляет этот буфер
        Memory &m_mem;

        // Текущая позиция в буфере
        size_t m_current_size;

        // Флаг, был ли добавлен разделитель сообщения
        bool m_headers_finalized;

        // Флаг: было ли закончено сообщение
        bool m_memory_finalized;

        // разделитель между заголовками
        static const char m_header_delimeter = ':';

        // разделитель между заголовками и полезной нагрузкой
        static const char m_memory_delimeter = '\n';

        // символ окончания сообщения
        static const char m_memory_finalizer = '\0';

    public:
        BufferView(Memory &mem);
        BufferView() = delete;
        virtual ~BufferView() {};

        // Запрещаем копирование
        BufferView(const BufferView &) = delete;
        BufferView &operator=(const BufferView &) = delete;

        // Разрешаем перемещение
        BufferView(BufferView &&other) = default;
        BufferView &operator=(BufferView &&other) = default;

        /**
         *  Публичный API для конечного приложения
         */
        /// @brief Очистка буфера
        void reset();

        /// @brief Получить максимальный размер буфера
        /// @return максимальный размер буфера
        size_t getCapacity() const;

        /// @brief Получить текущий размер буфера
        /// @return текущий размер буфера
        size_t getCurrentSize() const;

        /// @brief Обработчик завершения секции заголовков
        /// @return 1 - удачное закрытие заголовочноый секции, 0 - заголовочная секция не закрыта
        virtual bool finalizeHeader() = 0;

        /// @brief Обработчик завершения секции полезной нагрухки
        /// @return 1 - удачное закрытие секции полезной нагрузки, 0 - секция полезной нагрузки не закрыта
        virtual bool finalizePayload() = 0;

        /// @brief Вывод буфера в поток
        /// @param out поток для вывода
        friend std::ostream &
        operator<<(std::ostream &out, const BufferView &buffer);
    };

    // Буфер для записи в память
    class WriteBufferView : public BufferView
    {
    private:
        // была ли начата записть заголовков
        bool m_headers_initialized;

    public:
        WriteBufferView(Memory &mem);
        ~WriteBufferView() {};

        /// @brief Добавить заголовок отделенный символом ':' от других заголовков
        /// @param data данные для добавления заголовка
        /// @param len размер заголовка
        /// @return true, если заголовок добавлен, false, если не добавлен
        bool addHeader(const char *data, size_t len);

        /// @brief Добавить заголовок отделенный символом ':' от других заголовков
        /// @param header сам заголовок, который надо добавить
        /// @return true, если заголовок добавлен, false, если не добавлен
        bool addHeader(const std::string &header);

        /// @brief Завершает секцию заголовков, добавляя m_memory_delimeter ('\0') в память
        /// @return 1 - удачное закрытие заголовочноый секции, 0 - заголовочная секция не закрыта
        bool finalizeHeader() override;

        /// @brief Запись полезной нагрузки
        /// @param data данные на запись
        /// @param len размер данных
        /// @return 1 - данные записаны, 0 - данные не записаны
        bool setPayload(const char *data, size_t len);

        /// @brief Запись полезной нагрузки
        /// @param data данные на запись
        /// @return 1 - данные записаны, 0 - данные не записаны
        bool setPayload(const std::string &data);

        /// @brief Завершает секцию полезной нагрузки
        /// @return 1 - удачное закрытие секции полезной нагрузки, 0 - секция полезной нагрузки не закрыта
        bool finalizePayload() override;
    };

    // Буфер для чтения из памяти
    class ReadBufferView : public BufferView
    {
    public:
        ReadBufferView(Memory &mem);
        ~ReadBufferView() {};

        /// @brief Получение следующего заголовка в сообщении
        /// @return заголовок, либо std::nullopt, если заголовков больше нет
        std::optional<std::string_view> getHeader();

        /// @brief Поулчение полезной нагрузки
        /// @return полезная нагрузка, либо std::nullopt, если полезная нагрузка считана
        std::optional<std::string_view> getPayload();

        /// @brief Завершает секцию заголовков, добавляя m_memory_delimeter ('\0') в память
        /// @return 1 - удачное закрытие заголовочноый секции, 0 - заголовочная секция не закрыта
        bool finalizeHeader() override;
        
        /// @brief Завершает секцию полезной нагрузки
        /// @return 1 - удачное закрытие секции полезной нагрузки, 0 - секция полезной нагрузки не закрыта
        bool finalizePayload() override;
    };
}

#endif // !RIPC_SUB_MEM_HPP