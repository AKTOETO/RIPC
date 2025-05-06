#ifndef RIPC_SUB_MEM_HPP
#define RIPC_SUB_MEM_HPP

#include "types.hpp"
#include "context.hpp"
#include <string>

// структура, описывающая область памяти для общения
namespace ripc
{
    struct SubMem
    {
        RipcContext& m_context;
        void* m_addr;
        size_t m_size;
        bool m_is_mapped;

        SubMem() = delete;
        SubMem(RipcContext& context);
        ~SubMem();

        void mmap(int first_id, int second_id);
        void unmap();

        // чтение
        size_t readUntil(size_t offset, char* buffer, size_t buffer_size, char delim);
        size_t read(size_t offset, char* buffer, size_t buffer_size);
        std::string read(size_t offset = 0, size_t buffer_size = 0);

        // запись
        size_t write(size_t offset, char* buffer, size_t buffer_size);
        size_t write(size_t offset, std::string data);
    };
}

#endif // !RIPC_SUB_MEM_HPP