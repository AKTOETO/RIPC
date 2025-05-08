#ifndef RIPC_SUB_MEM_HPP
#define RIPC_SUB_MEM_HPP

#include "types.hpp"
#include "context.hpp"
#include <string>
#include <iostream>
#include <string>
#include <array>

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

        RipcContext &m_context;
        char *m_addr;
        size_t m_max_size;
        size_t m_current_size;
        bool m_is_mapped;

        void mmap(int first_id, int second_id);
        void unmap();

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
    class Buffer
    {
    private:
        friend class Client;
        friend class Server;

        std::array<char, SHM_REGION_PAGE_SIZE> m_data;
        size_t m_current_size;
        size_t m_max_size;


        /// @brief Установка максимального размера
        /// @param size запрашиваемый размер
        /// @return Если размер меньше размер общей области памяти, то вернется 1, иначе 0
        bool setMaxSize(size_t size);

    public:
        Buffer(const Memory &mem, size_t offset);
        Buffer();
        ~Buffer() = default;

        // Запрещаем копирование
        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;

        // Разрешаем перемещение
        Buffer(Buffer &&other) = default;
        Buffer &operator=(Buffer &&other) = default;

        /**
         *  Публичный API для конечного приложения
         */

        /// @brief Получить элемент памяти
        /// @param index индекс элемента в буфере
        /// @return элемент буфера
        const char &operator[](size_t index) const;

        /// @brief Получить сфрой указатель на буфер
        /// @return указатель на буфер
        const char *data() const;

        /// @brief Очистка буфера
        void clear();

        /// @brief Получить максимальный размер буфера
        /// @return максимальный размер буфера
        size_t capacity() const;

        /// @brief Получить текущий размер буфера
        /// @return текущий размер буфера
        size_t size() const;

        /// @brief Добавить в конец буфера новый элемент
        /// @param ch элемент для добавления
        /// @return Если элемент добавлен, вернет 1, если нет - вернет 0
        bool push_back(const char &ch);

        /// @brief Добавить в конец буфера новый элемент
        /// @param ch элемент для добавления
        /// @return Если элемент добавлен, вернет 1, если нет - вернет 0
        bool push_back(char &&ch);

        /// @brief Копирование данных в буфер
        /// @param source источник данных
        /// @param count количество данных
        /// @param offset сдвиг в конечном буфере, с которого начнется вставка новых элементов
        /// @return количество скопированных данных
        size_t copy_from(const char *source, size_t count, size_t offset = 0);
        size_t copy_from(const std::string &source, size_t offset = 0);

        /// @brief Вывод буфера в поток
        /// @param out поток для вывода
        friend std::ostream &operator<<(std::ostream &out, const Buffer &buffer);
    };
}

#endif // !RIPC_SUB_MEM_HPP