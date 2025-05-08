#include "ripc/submem.hpp"
#include "id_pack.h"
#include <iostream>
#include <sys/mman.h>
#include <string.h>

namespace ripc
{
    Memory::Memory(RipcContext &context)
        : m_context(context), m_addr(nullptr), m_is_mapped(false), m_max_size(-1), m_current_size(0)
    {
    }

    Memory::~Memory()
    {
        unmap();
    }

    void Memory::mmap(int first_id, int second_id)
    {
        // инициализирован ли контекст
        if (!m_context.isInitialized())
        {
            throw std::runtime_error("SubMem::mmap: Context is not initialized");
        }

        // запаковывание id {(client, 0) or (server, submem)}
        u32 packed_id = pack_ids(first_id, second_id);
        if (packed_id == (u32)-EINVAL)
        {
            throw std::runtime_error(
                "SubMem::mmap: " +
                std::to_string(first_id) + ": Failed to pack ID for mmap.");
        }

        off_t offset = (off_t)packed_id * m_context.getPageSize();
        std::cout << "SubMem::mmap: " << first_id << ": Attempting mmap with offset 0x"
                  << std::hex << offset << " (packed 0x" << packed_id << ")" << std::dec << std::endl;

        // запрос на отображение памяти
        char *addr = static_cast<char *>(
            ::mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, m_context.getFd(), offset));

        if (addr == MAP_FAILED)
        {
            int err_code = errno;
            throw std::runtime_error("SubMem::mmap: " +
                                     std::to_string(first_id) + ": mmap failed: " + strerror(err_code));
        }

        // запись результатов
        m_addr = addr;
        m_is_mapped = true;
        m_current_size = m_max_size = SHM_REGION_PAGE_SIZE;
    }
    void Memory::unmap()
    {
        if (!m_is_mapped)
        {
            std::cerr << "SubMem::unmap: memoru already unmapped\n";
            return;
        }

        if (!m_addr)
        {
            std::cerr << "SubMem::unmap: addres is empty\n";
        }

        if (munmap(m_addr, (m_max_size == -1 ? SHM_REGION_PAGE_SIZE : m_max_size)) != 0)
        {
            int err_code = errno;
            throw std::runtime_error("SubMem::unmap: munmap failed: " + std::string(strerror(err_code)));
        }
    }

    size_t Memory::readUntil(size_t offset, char *buffer, size_t buffer_size, char delim)
    {
        if (!m_is_mapped || !m_addr)
            throw std::logic_error("Memory::write: Shared memory is not mapped.");
        if (offset < 0 || offset >= m_max_size || !buffer || buffer_size < 0)
            throw std::invalid_argument("SubMem::readUntil: invalid argument");

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
        m_current_size = read_len;

        return read_len;
    }

    size_t Memory::read(size_t offset, char *buffer, size_t buffer_size)
    {
        if (!m_is_mapped || !m_addr)
            throw std::logic_error("Memory::write: Shared memory is not mapped.");
        if (offset < 0 || offset >= m_max_size || !buffer || buffer_size < 0)
            throw std::invalid_argument("SubMem::read: invalid argument");

        // высчитываем смещение
        size_t available = m_max_size - offset;
        size_t read_len = std::min(buffer_size, available);

        if (read_len > 0)
        {
            memcpy(buffer, static_cast<char *>(m_addr) + offset, read_len);
        }

        // сохраняем текущий размер буфера
        m_current_size = read_len;

        return read_len;
    }

    std::string Memory::read(size_t offset, size_t buffer_size)
    {
        if (!m_is_mapped || !m_addr)
            throw std::logic_error("Memory::write: Shared memory is not mapped.");
        if (offset < 0 || offset >= m_max_size || buffer_size < 0)
            throw std::invalid_argument("SubMem::read: invalid argument");

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
        if (!m_is_mapped || !m_addr)
            throw std::logic_error("Memory::write: Shared memory is not mapped.");
        if (!buffer || buffer_size < 0 || offset >= m_max_size)
            throw std::invalid_argument("Memory::write: Input buffer is null or offset bigger then max_size");

        // Определяем, сколько байт можно записать
        size_t available_space = m_max_size - offset;
        size_t write_len = std::min(buffer_size, available_space); // Реальное кол-во байт для записи

        // Копируем данные
        if (write_len > 0)
        {
            char *dest = static_cast<char *>(m_addr) + offset; // Указатель на место назначения
            memcpy(dest, buffer, write_len);
        }

        m_current_size = std::max(m_current_size, offset + write_len);

        if (write_len < buffer_size)
        {
            std::cerr << "Memory::write: Warning - Data truncated. Tried to write "
                      << buffer_size << " bytes, but only " << write_len
                      << " bytes fit at offset " << offset << "." << std::endl;
        }

        return write_len;
    }

    size_t Memory::write(size_t offset, std::string data)
    {
        if (!m_is_mapped || !m_addr)
            throw std::logic_error("Memory::write: Shared memory is not mapped.");
        return write(offset, data.data(), data.size());
    }

    bool Memory::add(size_t offset, char ch)
    {
        if (!m_is_mapped || !m_addr)
            throw std::logic_error("Memory::write: Shared memory is not mapped.");
        if (offset >= m_max_size)
            throw std::invalid_argument("Memory::write: Offset bigger then max_size.");

        if (offset < m_max_size)
        {
            char *dest = static_cast<char *>(m_addr) + offset;
            *dest = ch;
            return true;
        }
        return false;
    }

    bool Buffer::setMaxSize(size_t size)
    {
        if (size <= SHM_REGION_PAGE_SIZE)
        {
            m_max_size = size;
            return true;
        }
        return false;
    }

    /**
     * --- BUFFER ---
     */
    Buffer::Buffer(const Memory &mem, size_t offset)
        : m_current_size(0), m_max_size(SHM_REGION_PAGE_SIZE)
    {
        if (!mem.m_is_mapped)
            throw std::logic_error("Buffer::Buffer: Memory not mapped");

        copy_from(mem.m_addr + offset, mem.m_current_size);
    }

    Buffer ::Buffer()
        : m_current_size(0), m_max_size(SHM_REGION_PAGE_SIZE)
    {
    }

    const char &Buffer::operator[](size_t index) const
    {
        return m_data[index];
    }

    const char *Buffer::data() const
    {
        return m_data.data();
    }

    void Buffer::clear()
    {
        m_current_size = 0;
    }

    size_t Buffer::capacity() const
    {
        return m_max_size;
    }

    size_t Buffer::size() const
    {
        return m_current_size;
    }

    bool Buffer::push_back(const char &ch)
    {
        if (m_current_size < m_max_size)
        {
            m_data[m_current_size++] = ch;
            return true;
        }
        return false;
    }

    bool Buffer::push_back(char &&ch)
    {
        if (m_current_size < m_max_size)
        {
            m_data[m_current_size++] = std::move(ch);
            return true;
        }
        return false;
    }

    size_t Buffer::copy_from(const char *source, size_t count, size_t offset)
    {
        if (!source || count == 0)
            return 0;
        if (offset >= m_max_size)
            return 0;

        size_t available_space = m_max_size - offset;
        size_t elements_to_copy = std::min(count, available_space);

        m_data.fill('\0');

        for (size_t i = 0; i < elements_to_copy; ++i)
        {
            m_data[offset + i] = source[i];
        }

        m_current_size = std::max(m_current_size, offset + elements_to_copy);

        return elements_to_copy;
    }

    size_t Buffer::copy_from(const std::string &source, size_t offset)
    {
        return copy_from(source.data(), source.size(), offset);
    }
    std::ostream &operator<<(std::ostream &out, const Buffer &buffer)
    {
        out.write(buffer.data(), buffer.size());
        return out;
    }

} // namespace ripc
