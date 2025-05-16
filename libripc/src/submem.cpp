#include "ripc/submem.hpp"
#include "id_pack.h"
#include "ripc/logger.hpp"
#include <cstring>
#include <iostream>
#include <string.h>
#include <sys/mman.h>

namespace ripc
{

#define CHECK_MAPPED_IR(in, val)                                                                                       \
    {                                                                                                                  \
        if (!in.m_is_mapped)                                                                                           \
        {                                                                                                              \
            LOG_ERR("memory not mmapped");                                                                             \
            return val;                                                                                                \
        }                                                                                                              \
    }

#define CHECK_MMAPED_R(val) CHECK_MAPPED_IR((*this), val)

#define CHECK_MMAPED CHECK_MMAPED_R(false)

#define CHECK_ADDR_IR(in, val)                                                                                         \
    {                                                                                                                  \
        if (!in.m_addr)                                                                                                \
        {                                                                                                              \
            LOG_ERR("addres is empty");                                                                                \
            return val;                                                                                                \
        }                                                                                                              \
    }

#define CHECK_ADDR_R(val) CHECK_ADDR_IR((*this), val)

#define CHECK_ADDR CHECK_ADDR_R(false)

#define CHECK_OFFSET_R(val)                                                                                            \
    {                                                                                                                  \
        if (offset < 0 || offset >= m_max_size)                                                                        \
        {                                                                                                              \
            LOG_ERR("invalid offset");                                                                                 \
            return val;                                                                                                \
        }                                                                                                              \
    }

#define CHECK_OFFSET CHECK_OFFSET_R(false)

    Memory::Memory(RipcContext &context) : m_context(context), m_addr(nullptr), m_is_mapped(false), m_max_size(-1)
    // m_current_size(0)
    {
    }

    Memory::~Memory()
    {
        unmap();
    }

    bool Memory::mmap(int first_id, int second_id)
    {
        // инициализирован ли контекст
        if (!m_context.isInitialized())
        {
            // throw std::runtime_error("SubMem::mmap: Context is not initialized");
            LOG_ERR("Context is not initialized");
            return false;
        }

        // запаковывание id {(client, 0) or (server, submem)}
        u32 packed_id = pack_ids(first_id, second_id);
        if (packed_id == (u32)-EINVAL)
        {
            // throw std::runtime_error(
            //     "SubMem::mmap: " +
            //     std::to_string(first_id) + ": Failed to pack ID for mmap.");
            LOG_ERR("(%d, %d) failed to pack id for mmap", first_id, second_id);
            return false;
        }

        off_t offset = (off_t)packed_id * m_context.getPageSize();
        // std::cout << "SubMem::mmap: " << first_id << ": Attempting mmap with offset
        // 0x"
        //           << std::hex << offset << " (packed 0x" << packed_id << ")" <<
        //           std::dec << std::endl;
        LOG_INFO("%d Attempt to call mmap with offset 0x%x (packed 0x%x)", first_id, offset, packed_id);

        // запрос на отображение памяти
        char *addr = static_cast<char *>(
            ::mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, m_context.getFd(), offset));

        if (addr == MAP_FAILED)
        {
            int err_code = errno;
            // throw std::runtime_error("SubMem::mmap: " +
            //                          std::to_string(first_id) + ": mmap failed: " +
            //                          strerror(err_code));
            LOG_ERR("%d mmap failed: %s", first_id, strerror(err_code));
        }

        // запись результатов
        m_addr = addr;
        m_is_mapped = true;
        // m_current_size =
        m_max_size = SHM_REGION_PAGE_SIZE;

        return true;
    }
    bool Memory::unmap()
    {
        // if (!m_is_mapped)
        // {
        //     // std::cerr << "SubMem::unmap: memory already unmapped\n";
        //     LOG_ERR("memory not mmapped");
        //     return false;
        // }

        // if (!m_addr)
        // {
        //     std::cerr << "SubMem::unmap: addres is empty\n";
        // }

        CHECK_MMAPED_R(true)
        CHECK_ADDR

        if (munmap(m_addr, (m_max_size == -1 ? SHM_REGION_PAGE_SIZE : m_max_size)) != 0)
        {
            int err_code = errno;
            // throw std::runtime_error("SubMem::unmap: munmap failed: " +
            // std::string(strerror(err_code)));
            LOG_ERR("munmap failed: ", strerror(errno));
            return false;
        }

        m_is_mapped = false;
        return true;
    }

    char *Memory::end() const
    {
        return static_cast<char *>(m_addr) + m_max_size;
    }

    char *Memory::find(size_t offset, char ch)
    {
        CHECK_MMAPED_R(nullptr);
        CHECK_ADDR_R(nullptr)
        CHECK_OFFSET_R(nullptr)

        char *pos = static_cast<char *>(m_addr) + offset;
        char *end_b = end();
        size_t i = 0;
        for (;; i++, pos++)
        {
            //LOG_INFO("cur char[%i]: %c = %i", i, *pos, int(*pos))
            if ((*pos) == ch)
            {
                return pos;
            }
            else if (pos == end_b)
            {
                return end_b;
            }
        }
    }

    char *Memory::find(size_t offset, const char *ch, size_t len)
    {
        CHECK_MMAPED_R(nullptr);
        CHECK_ADDR_R(nullptr)
        CHECK_OFFSET_R(nullptr)

        //LOG_INFO("finding delims");
        for (size_t i = 0; i < len; i++)
        {
            //LOG_INFO("Delim: %i = '%c'", int(ch[i]), ch[i]);
            char *pos = find(offset, ch[i]);

            if (pos != end())
            {
                return pos;
            }
        }
        return end();
    }

    char *Memory::find(size_t offset, const std::string &chars)
    {
        CHECK_MMAPED_R(nullptr);
        CHECK_ADDR_R(nullptr)
        CHECK_OFFSET_R(nullptr)

        for (size_t i = offset; i < m_max_size; i++)
        {
            char *cur_char = static_cast<char *>(m_addr) + i;

            if (chars.find(*cur_char) != std::string_view::npos)
            {
                return cur_char;
            }
        }
        return end();
    }

    size_t Memory::readUntil(size_t offset, char *buffer, size_t buffer_size, char delim)
    {
        CHECK_MMAPED_R(-1);
        CHECK_ADDR_R(-1);
        CHECK_OFFSET_R(-1);
        // if (!m_is_mapped || !m_addr)
        //     throw std::logic_error("Memory::write: Shared memory is not mapped.");
        // if (offset < 0 || offset >= m_max_size || !buffer || buffer_size < 0)
        //     throw std::invalid_argument("SubMem::readUntil: invalid argument");
        if (!buffer || buffer_size < 0)
        {
            LOG_ERR("invalid buffer");
            return -1;
        }

        // высчитываем смещение
        size_t available = m_max_size - offset;
        size_t read_len = std::min(buffer_size, available);

        if (read_len > 0)
        {
            void *result_ptr = memccpy(buffer, static_cast<char *>(m_addr) + offset, delim, read_len);
            if (result_ptr != nullptr)
                read_len = static_cast<char *>(result_ptr) - buffer;
        }

        // сохраняем текущий размер буфера
        // m_current_size = read_len;

        return read_len;
    }

    size_t Memory::read(size_t offset, char *buffer, size_t buffer_size)
    {
        // if (!m_is_mapped || !m_addr)
        //     throw std::logic_error("Memory::write: Shared memory is not mapped.");
        // if (offset < 0 || offset >= m_max_size || !buffer || buffer_size < 0)
        //     throw std::invalid_argument("SubMem::read: invalid argument");
        CHECK_MMAPED_R(-1);
        CHECK_ADDR_R(-1);
        CHECK_OFFSET_R(-1);
        if (!buffer || buffer_size < 0)
        {
            LOG_ERR("invalid buffer");
            return -1;
        }

        // высчитываем смещение
        size_t available = m_max_size - offset;
        size_t read_len = std::min(buffer_size, available);

        if (read_len > 0)
        {
            memcpy(buffer, static_cast<char *>(m_addr) + offset, read_len);
        }

        // сохраняем текущий размер буфера
        // m_current_size = read_len;

        return read_len;
    }

    std::string Memory::read(size_t offset, size_t buffer_size)
    {
        // if (!m_is_mapped || !m_addr)
        //     throw std::logic_error("Memory::write: Shared memory is not mapped.");
        // if (offset < 0 || offset >= m_max_size || !buffer || buffer_size < 0)
        //     throw std::invalid_argument("SubMem::read: invalid argument");
        CHECK_MMAPED_R("");
        CHECK_ADDR_R("");
        CHECK_OFFSET_R("");
        if (buffer_size < 0)
        {
            LOG_ERR("invalid buffer");
            return "";
        }

        // высчитываем смещение
        size_t available = m_max_size - offset;
        size_t read_len = std::min(buffer_size, available);

        // создаем буфер
        std::string out(read_len, '\0');

        if (read_len > 0)
        {
            char *str_data = out.data();
            auto read = memcpy(str_data, static_cast<char *>(m_addr) + offset, read_len);
        }

        return out;
    }

    size_t Memory::write(size_t offset, const char *buffer, size_t buffer_size)
    {
        // if (!m_is_mapped || !m_addr)
        //     throw std::logic_error("Memory::write: Shared memory is not mapped.");
        // if (!buffer || buffer_size < 0 || offset >= m_max_size)
        //     throw std::invalid_argument("Memory::write: Input buffer is null or
        //     offset bigger then max_size");
        CHECK_MMAPED_R(-1);
        CHECK_ADDR_R(-1);
        CHECK_OFFSET_R(-1);
        if (!buffer || buffer_size < 0)
        {
            LOG_ERR("invalid buffer");
            return -1;
        }

        // Определяем, сколько байт можно записать
        size_t available_space = m_max_size - offset;
        size_t write_len = std::min(buffer_size, available_space); // Реальное кол-во байт для записи

        // Копируем данные
        if (write_len > 0)
        {
            char *dest = static_cast<char *>(m_addr) + offset; // Указатель на место назначения
            memcpy(dest, buffer, write_len);
        }

        // m_current_size = std::max(m_current_size, offset + write_len);

        if (write_len < buffer_size)
        {
            // std::cerr << "Memory::write: Warning - Data truncated. Tried to write "
            //           << buffer_size << " bytes, but only " << write_len
            //           << " bytes fit at offset " << offset << "." << std::endl;
            LOG_WARN("Data truncated. Tried to write %d bytes, but only %d bytes fit "
                     "at offset %d",
                     buffer_size, write_len, offset);
        }

        return write_len;
    }

    size_t Memory::write(size_t offset, std::string data)
    {
        CHECK_MMAPED_R(-1);
        CHECK_ADDR_R(-1);
        // if (!m_is_mapped || !m_addr)
        //     throw std::logic_error("Memory::write: Shared memory is not mapped.");
        return write(offset, data.data(), data.size());
    }

    // size_t Memory::write(const char *data, size_t len)
    //{
    //     return write(m_current_size, data, len);
    // }

    bool Memory::add(size_t offset, char ch)
    {
        CHECK_MMAPED;
        CHECK_ADDR;
        CHECK_OFFSET;

        char *dest = static_cast<char *>(m_addr) + offset;
        *dest = ch;
        return true;
    }

    BufferView::BufferView(Memory &mem)
        : m_mem(mem), m_headers_finalized(false), m_memory_finalized(false), m_current_size(0)
    {
        if (!mem.m_is_mapped)
            LOG_ERR("Memory not mapped");
        //    throw std::logic_error("Buffer::Buffer: Memory not mapped");
    }

    void BufferView::reset()
    {
        m_current_size = 0;
        m_headers_finalized = 0;
        m_memory_finalized = 0;
    }

    size_t BufferView::getCapacity() const
    {
        return m_mem.m_max_size;
    }

    size_t BufferView::getCurrentSize() const
    {
        return m_current_size;
    }

    std::ostream &operator<<(std::ostream &out, const BufferView &buffer)
    {
        // Проверяем, что память, на которую указывает BufferView, валидна и
        // отображена
        if (!buffer.m_mem.m_is_mapped || !buffer.m_mem.m_addr)
        {
            out << "[BufferView: Memory not mapped or invalid]";
            return out;
        }

        // const char *start_ptr = static_cast<const char *>(buffer.m_mem.m_addr);
        // size_t buffer_capacity = buffer.m_mem.m_max_size;
        // size_t len_to_write = 0;

        // // Ищем символ m_memory_finalizer ('\0') или конец буфера
        // const char *end_marker_ptr = static_cast<const char *>(
        //     memchr(start_ptr, BufferView::m_memory_finalizer, buffer_capacity));

        // if (end_marker_ptr != nullptr)
        // {
        //     // Финализатор ('\0') найден, выводим данные до него
        //     len_to_write = static_cast<size_t>(end_marker_ptr - start_ptr);
        // }
        // else
        // {
        //     // Финализатор ('\0') не найден, выводим все до конца емкости буфера
        //     len_to_write = buffer_capacity;
        // }

        // Выводим определенное количество байт.
        // Это безопасно, даже если len_to_write == 0.
        // Использование out.write() корректно обработает встроенные нулевые символы,
        // если они ВНУТРИ len_to_write (хотя по логике до m_memory_finalizer их быть
        // не должно).

        const char *start_ptr;
        size_t len_to_write = buffer.getCharStr(&start_ptr);

        if (len_to_write > 0)
        {
            out.write(start_ptr, len_to_write);
        }

        return out;
    }

    WriteBufferView::WriteBufferView(Memory &mem) : BufferView(mem), m_headers_initialized(false)
    {
    }

    bool WriteBufferView::addHeader(const char *data, size_t len)
    {
        if (m_memory_finalized)
        {
            // std::cerr << "BufferView Error: Message already finalized\n";
            LOG_ERR("Message already finalized");
            return false;
        }
        if (m_headers_finalized)
        {
            // std::cerr << "BufferView Error: Cannot add headers after payload section
            // started." << std::endl;
            LOG_ERR("Cannot add headers after payload section started");
            return false;
        }
        if (!m_mem.m_is_mapped)
        {
            // std::cerr << "BufferView Error: memory is no mapped" << std::endl;
            LOG_ERR("memory is not mapped");
            return false;
        }

        // если это первый заголовок, то разделитель между заголовками ставить не надо
        if (m_headers_initialized)
        {
            if (!m_mem.add(m_current_size, m_header_delimeter))
            {
                // std::cerr << "BufferView Error: cannot add header delimeter" <<
                // std::endl;
                LOG_ERR("cannot add header delimeter");
                return false;
            }
            m_current_size++;
            m_headers_initialized = true;
        }

        // записываем данные
        auto size = m_mem.write(m_current_size, data, len);

        // сохраняем размер
        m_current_size += size;

        // говрим, что первый заголовок записан
        m_headers_initialized = 1;

        return size == len;
    }

    bool WriteBufferView::addHeader(const std::string &header)
    {
        return addHeader(header.data(), header.size());
    }

    bool WriteBufferView::finalizeHeader()
    {
        if (m_memory_finalized)
        {
            if (m_headers_finalized)
            {
                LOG_INFO("memory already finalized");
                return true;
            }
            // std::cerr << "BufferView Error: Message already finalized\n";
            LOG_ERR("Message already finalized without headers");
            return false;
        }
        if (m_headers_finalized)
        {
            LOG_INFO("memory already finalized");
            return true;
        }

        if (!m_mem.m_is_mapped)
        {
            // std::cerr << "BufferView Error: memory is no mapped" << std::endl;
            LOG_ERR("memory is not mapped");
            return false;
        }

        // добавляем разделитель
        if (!m_mem.add(m_current_size, m_memory_delimeter))
        {
            // std::cerr << "BufferView Error: Cannot add message delimeter" <<
            // std::endl;
            LOG_ERR("cannot add message delimeter");
            return false;
        }
        m_current_size++;
        m_headers_finalized = true;
        LOG_INFO("headers finalized");
        return true;
    }

    bool WriteBufferView::setPayload(const char *data, size_t len)
    {
        // если сообщение уже записано
        if (m_memory_finalized)
        {
            // std::cerr << "BufferView Error: Message already finalized\n";
            LOG_ERR("Message already finalized");
            return false;
        }

        // если запись заголовков не закончена, заканчиваем ее
        if (!m_headers_finalized)
        {
            // если не получилось закончить запись заголовков
            if (!finalizeHeader())
            {
                // std::cerr << "BufferView::addPayload: cannot finalize headers\n";
                LOG_ERR("cannot finalize headers");
                return false;
            }
        }

        size_t size = 0;
        // записываем данные
        if ((size = m_mem.write(m_current_size, data, len)) <= 0)
        {
            // std::cerr << "BufferView::addPayload: payload was not written\n";
            LOG_ERR("payload was not written");
            return false;
        }
        m_current_size += size;

        return finalizePayload();
    }

    bool WriteBufferView::setPayload(const std::string &data)
    {
        return setPayload(data.data(), data.length());
    }

    bool WriteBufferView::finalizePayload()
    {

        if (!m_mem.m_addr || !m_mem.m_is_mapped)
            return false;
        if (m_memory_finalized)
        {
            LOG_INFO("memory already finalized");
            return true;
        }
        if (!m_headers_finalized)
        {
            if (!finalizeHeader())
            {
                // std::cerr << "WriteBufferView::finalizePayload: cannot finalize
                // headers\n";
                LOG_ERR("cannot finalize headers");
                return false;
            }
        }

        // записываем конец сообщения
        if (!m_mem.add(m_current_size, m_memory_finalizer))
        {
            // std::cerr << "WriteBufferView::finalizePayload: cannot finalize
            // memory\n";
            LOG_ERR("cannot finalize memory");
            return false;
        }
        m_current_size++;

        m_memory_finalized = 1;

        LOG_INFO("memory finalized");
        return true;
    }

    size_t BufferView::getCharStr(const char **ch) const
    {
        // Проверяем, что память, на которую указывает BufferView, валидна и
        // отображена
        if (!m_mem.m_is_mapped || !m_mem.m_addr)
        {
            LOG_WARN("Memory not mapped or invalid");
            return -1;
        }

        *ch = static_cast<const char *>(m_mem.m_addr);
        size_t buffer_capacity = m_mem.m_max_size;
        size_t len_to_write = 0;

        // Ищем символ m_memory_finalizer ('\0') или конец буфера
        const char *end_marker_ptr = static_cast<const char *>(memchr(*ch, m_memory_finalizer, buffer_capacity));

        if (end_marker_ptr != nullptr)
        {
            // Финализатор ('\0') найден, выводим данные до него
            len_to_write = static_cast<size_t>(end_marker_ptr - (*ch));
        }
        else
        {
            // Финализатор ('\0') не найден, выводим все до конца емкости буфера
            len_to_write = buffer_capacity;
        }
        return len_to_write;
    }

    std::string BufferView::getStr() const
    {
        const char *ch;
        auto size = getCharStr(&ch);
        return std::string(ch, size);
    }

    ReadBufferView::ReadBufferView(Memory &mem) : BufferView(mem)
    {
    }

    std::optional<std::string_view> ReadBufferView::getHeader()
    {
        if (m_memory_finalized)
        {
            // std::cerr << "BufferView Error: Message already finalized\n";
            LOG_WARN("Message already finalized");
            return std::nullopt;
        }
        if (m_headers_finalized)
        {
            // std::cerr << "BufferView Error: Cannot add headers after payload section
            // started." << std::endl;
            LOG_WARN("There is no more headers");
            return std::nullopt;
        }
        if (!m_mem.m_is_mapped)
        {
            // std::cerr << "BufferView Error: memory is no mapped" << std::endl;
            LOG_ERR("memory is not mapped");
            return std::nullopt;
        }

        // ищем конец первого заголовка
        char delims[3] = {m_header_delimeter, m_memory_delimeter, m_memory_finalizer};
        char *start = m_mem.m_addr + m_current_size;
        char *end = m_mem.find(m_current_size, delims, 3);

        // Разделители не найдены, скорее всего строка кончилась
        if (end == m_mem.end())
        {
            LOG_ERR("There is not delimeter between buffer and payload ");
            return std::nullopt;
        }

        // размер выделенной области
        size_t size = end - start;

        // если конец равен символу разделения заголовков,
        // то это новый заголовок
        if (*end == m_header_delimeter)
        {
            m_current_size += size + 1;
            LOG_INFO("headers delimeter is found");
            return std::string_view(start, size);
        }
        // если это символ разделения заголовков и полезной части
        // то финализируем заголовки
        else if (*end == m_memory_delimeter)
        {
            m_current_size += size + 1;
            // finalizeHeader();
            m_headers_finalized = 1;
            LOG_INFO("headers with payload delimeter is found");
            return std::string_view(start, size);
        }
        // если это символ конца сообщения
        else if (*end == m_memory_finalizer)
        {
            m_current_size += size + 1;
            //finalizePayload();
            m_headers_finalized = 1;
            m_memory_finalized = 1;
            LOG_INFO("end of memory is found");
            return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<std::string_view> ReadBufferView::getPayload()
    {
        LOG_INFO("Getting payload...");
        if (m_memory_finalized)
        {
            // std::cerr << "BufferView Error: Message already finalized\n";
            LOG_INFO("Message already finalized");
            return std::nullopt;
        }
        if (!m_headers_finalized)
        {
            if (!finalizeHeader())
            {
                // std::cerr << "WriteBufferView::finalizePayload: cannot finalize
                // headers\n";
                LOG_ERR("cannot finalize headers");
                return std::nullopt;
            }
        }
        if (!m_mem.m_is_mapped)
        {
            // std::cerr << "BufferView Error: memory is no mapped" << std::endl;
            LOG_ERR("memory is no mapped");
            return std::nullopt;
        }

        // ищем конец первого заголовка
        char *start = m_mem.m_addr + m_current_size;
        char *end = m_mem.find(m_current_size, m_memory_finalizer);

        // если это символ конца сообщения,
        // то найдена полезная нагрузка
        if (*end == m_memory_finalizer)
        {
            // размер выделенной области
            size_t size = end - start;
            m_current_size += size;
            //finalizePayload();
            m_memory_finalized = 1;
            //LOG_INFO("Got payload: '%s'", std::string_view(start, size));
            return std::string_view(start, size);
        }
        LOG_ERR("Empty payload");

        return std::nullopt;
    }

    bool ReadBufferView::finalizeHeader()
    {
        if (m_memory_finalized)
        {
            if (m_headers_finalized)
                return true;

            LOG_ERR("Message already finalized");
            return false;
        }
        if (m_headers_finalized)
            return true;

        if (!m_mem.m_is_mapped)
        {
            LOG_ERR("memory is no mapped");
            return false;
        }

        LOG_INFO("Searching for char m_memory_delimeter");
        // этот метод пролистал m_current_position до начала полезной нагрузки
        char *delim = m_mem.find(m_current_size, m_memory_delimeter);
        LOG_INFO("Got smth");

        if (!delim || delim == m_mem.end())
        {
            LOG_ERR("there is no a memory delimeter");
            return false;
        }

        // если это разделитель между заголовком и сообщением
        if (*delim == m_memory_delimeter)
        {
            m_current_size = delim - m_mem.m_addr + 1;
            m_headers_finalized = true;
            LOG_INFO("Headers finalized");
            return true;
        }

        LOG_ERR("Cannot finalize headers");
        return false;
    }

    bool ReadBufferView::finalizePayload()
    {
        if (!m_mem.m_addr || !m_mem.m_is_mapped)
            return false;
        if (m_memory_finalized)
            return true;
        if (!m_headers_finalized)
        {
            m_headers_finalized = 1;
            // if (!finalizeHeader())
            //{
            //     LOG_ERR("cannot finalize headers");
            //     return false;
            // }
        }

        // перематываем до символа конца полезной нагрузки
        char *delim = m_mem.find(m_current_size, m_memory_finalizer);

        if (!delim)
        {
            LOG_ERR("cannot find message finisher");
            return false;
        }
        else if (*delim == m_memory_finalizer || delim == m_mem.end())
        {
            m_memory_finalized = 1;
            m_current_size = m_mem.m_max_size;
            return true;
        }

        // // если мы дошли до конца
        // else if (delim == m_mem.end())
        //     m_current_size = m_mem.m_max_size;
        // else
        //     m_current_size = delim - m_mem.m_addr + 1;

        // m_memory_finalized = 1;
        return false;
    }

} // namespace ripc
