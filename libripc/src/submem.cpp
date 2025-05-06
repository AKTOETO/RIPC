#include "ripc/submem.hpp"
#include "id_pack.h"
#include <iostream>
#include <sys/mman.h>
#include <string.h>

namespace ripc
{
    SubMem::SubMem(RipcContext &context)
        : m_context(context), m_addr(nullptr), m_is_mapped(false), m_size(-1)
    {
    }

    SubMem::~SubMem()
    {
        unmap();
    }

    void SubMem::mmap(int first_id, int second_id)
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
        void *addr = ::mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, m_context.getFd(), offset);

        if (addr == MAP_FAILED)
        {
            int err_code = errno;
            throw std::runtime_error("SubMem::mmap: " +
                                     std::to_string(first_id) + ": mmap failed: " + strerror(err_code));
        }

        // запись результатов
        m_addr = addr;
        m_is_mapped = true;
        m_size = SHM_REGION_PAGE_SIZE;
    }
    void SubMem::unmap()
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

        if (munmap(m_addr, (m_size == -1 ? SHM_REGION_PAGE_SIZE : m_size)) != 0)
        {
            int err_code = errno;
            throw std::runtime_error("SubMem::unmap: munmap failed: " + std::string(strerror(err_code)));
        }
    }

    size_t SubMem::readUntil(size_t offset, char *buffer, size_t buffer_size, char delim)
    {
        if (offset < 0 || offset >= m_size || !buffer || buffer_size < 0)
            throw std::invalid_argument("SubMem::readUntil: invalid argument");

        // высчитываем смещение
        size_t available = m_size - offset;
        size_t read_len = std::min(buffer_size, available);

        if (read_len > 0)
        {
            void *result_ptr = memccpy(buffer, static_cast<char *>(m_addr) + offset, delim, read_len);
            if (result_ptr != nullptr)
                read_len = static_cast<char *>(result_ptr) - buffer;
        }

        return read_len;
    }

    size_t SubMem::read(size_t offset, char *buffer, size_t buffer_size)
    {
        if (offset < 0 || offset >= m_size || !buffer || buffer_size < 0)
            throw std::invalid_argument("SubMem::read: invalid argument");

        // высчитываем смещение
        size_t available = m_size - offset;
        size_t read_len = std::min(buffer_size, available);

        if (read_len > 0)
        {
            memcpy(buffer, static_cast<char *>(m_addr) + offset, read_len);
        }

        return read_len;
    }

    std::string SubMem::read(size_t offset, size_t buffer_size)
    {
        if (offset < 0 || offset >= m_size || buffer_size < 0)
            throw std::invalid_argument("SubMem::read: invalid argument");

        // высчитываем смещение
        size_t available = m_size - offset;
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

    size_t SubMem::write(size_t offset, char *buffer, size_t buffer_size)
    {
        return size_t();
    }
    
    size_t SubMem::write(size_t offset, std::string data)
    {
        return write(offset, data.data(), data.size());
    }

} // namespace ripc
