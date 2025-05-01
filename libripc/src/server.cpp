#include "ripc/server.hpp"
#include "ripc/context.hpp"
#include "ripc.h"
#include "id_pack.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm> // std::find_if

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif
// Определим константы здесь, если они не в ripc_types.hpp
constexpr size_t MAX_SERVER_CLIENTS_INTERNAL = 16;
constexpr size_t MAX_SERVER_SHM_INTERNAL = 16;

namespace ripc
{

    // Приватный конструктор
    Server::Server(RipcContext &ctx, const std::string &server_name)
        : context(ctx), name(server_name),
          connections(MAX_SERVER_CLIENTS_INTERNAL), // Используем константу или значение из types.hpp
          mappings(MAX_SERVER_SHM_INTERNAL)
    {
        if (name.empty() || name.length() >= MAX_SERVER_NAME)
        {
            throw std::invalid_argument("Invalid server name.");
        }
        std::cout << "Server '" << name << "': Basic construction." << std::endl;
        // Векторы инициализируются по умолчанию
    }

    // Приватный init
    void Server::init()
    {
        if (initialized)
            return;
        std::cout << "Server '" << name << "': init() called by EntityManager." << std::endl;

        server_registration reg_data;
        strncpy(reg_data.name, name.c_str(), MAX_SERVER_NAME - 1);
        reg_data.name[MAX_SERVER_NAME - 1] = '\0';
        reg_data.server_id = -1;

        if (ioctl(context.getFd(), IOCTL_REGISTER_SERVER, &reg_data) < 0)
        {
            int err_code = errno;
            throw std::runtime_error("Server init failed: IOCTL_REGISTER_SERVER for '" + name + "': " + strerror(err_code));
        }
        if (!IS_ID_VALID(reg_data.server_id))
        {
            throw std::runtime_error("Server init failed: Kernel returned invalid server_id: " + std::to_string(reg_data.server_id));
        }

        this->server_id = reg_data.server_id;
        initialized = true;
        std::cout << "Server '" << name << "' initialized with ID " << server_id << "." << std::endl;
    }

    // Деструктор
    Server::~Server()
    {
        std::cout << "Server '" << name << "' (ID: " << server_id << ") destructing..." << std::endl;
        cleanup_mappings();
        // Отмена регистрации в ядре делается менеджером
    }

    // Приватная очистка mmap
    void Server::cleanup_mappings()
    {
        int unmap_count = 0;
        for (auto &map_info : mappings)
        {
            if (map_info.mapped && map_info.addr != MAP_FAILED)
            {
                int current_shm_id = map_info.shm_id; // Копируем для лога
                void *current_addr = map_info.addr;
                size_t current_size = map_info.size;

                map_info.mapped = false;
                map_info.addr = MAP_FAILED;
                map_info.size = 0;

                if (munmap(current_addr, current_size) == 0)
                {
                    unmap_count++;
                }
                else
                {
                    perror(("Server " + std::to_string(server_id) + ": munmap failed for shm_id " + std::to_string(current_shm_id)).c_str());
                }
            }
            // Сбрасываем неактивные слоты тоже на всякий случай
            // map_info.shm_id = -1; // Не обязательно, если инициализация правильная
        }
        if (unmap_count > 0)
        {
            std::cout << "Server " << server_id << ": Unmapped " << unmap_count << " regions." << std::endl;
        }
        // Очищаем вектор или сбрасываем shm_id? Зависит от логики findOrCreate
        for (auto &map_info : mappings)
            map_info.shm_id = -1; // Сбросим ID для чистоты
    }

    // Приватные проверки
    void Server::checkInitialized() const
    {
        if (!initialized)
            throw std::logic_error("Server '" + name + "' (ID: " + std::to_string(server_id) + ") is not initialized.");
    }

    // --- Публичные методы ---
    int Server::getId() const { return server_id; }
    const std::string &Server::getName() const { return name; }
    bool Server::isInitialized() const { return initialized; }

    void Server::mmapSubmemory(int shm_id)
    {
        checkInitialized();
        if (!IS_ID_VALID(shm_id))
            throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));

        ServerShmMapping *mapping = internal_findOrCreateShmMapping(shm_id);
        if (!mapping)
        {
            // internal_findOrCreateShmMapping уже вывел ошибку, если не смог создать
            throw std::runtime_error("Server " + std::to_string(server_id) + ": Failed to get mapping slot for shm_id " + std::to_string(shm_id));
        }

        if (mapping->mapped)
        {
            // std::cout << "Server " << server_id << ": shm_id " << shm_id << " already mapped." << std::endl;
            return;
        }

        u32 packed_id = pack_ids(server_id, shm_id);
        if (packed_id == (u32)-EINVAL)
            throw std::logic_error("Failed to pack IDs for mmap.");

        off_t offset = (off_t)packed_id * context.getPageSize();
        std::cout << "Server " << server_id << ": Attempting mmap for shm_id " << shm_id
                  << " (offset 0x" << std::hex << offset << ", packed 0x" << packed_id << ")" << std::dec << std::endl;

        void *addr = ::mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, context.getFd(), offset);

        if (addr == MAP_FAILED)
        {
            int err_code = errno;
            throw std::runtime_error("Server " + std::to_string(server_id) + ": mmap failed for shm_id " + std::to_string(shm_id) + ": " + strerror(err_code));
        }

        mapping->addr = addr;
        mapping->size = SHM_REGION_PAGE_SIZE;
        mapping->mapped = true;
        std::cout << "Server " << server_id << ": Mapped shm_id " << shm_id << " at " << addr << std::endl;
    }

    size_t Server::writeToClient(int client_id, size_t offset, const void *data, size_t size)
    {
        checkInitialized();
        if (!IS_ID_VALID(client_id))
            throw std::invalid_argument("Invalid client_id: " + std::to_string(client_id));
        if (!data && size > 0)
            throw std::invalid_argument("Invalid data pointer for write.");

        // 1. Найти активное соединение
        int conn_idx = internal_findConnectionIndex(client_id);
        if (conn_idx == -1)
        {
            throw std::runtime_error("Server " + std::to_string(server_id) + ": No active connection found for client ID " + std::to_string(client_id));
        }
        int target_shm_id = connections[conn_idx].shm_id;

        // 2. Найти/создать и отобразить SHM маппинг
        ServerShmMapping *mapping = internal_findOrCreateShmMapping(target_shm_id);
        if (!mapping)
        {
            throw std::runtime_error("Server " + std::to_string(server_id) + ": Failed to get mapping slot for shm_id " + std::to_string(target_shm_id));
        }
        if (!mapping->mapped)
        {
            try
            {
                mmapSubmemory(target_shm_id); // Попытка mmap по требованию
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error("Server " + std::to_string(server_id) + ": Cannot write, memory shm_id " + std::to_string(target_shm_id) + " not mapped: " + e.what());
            }
            if (!mapping->mapped)
                throw std::logic_error("Internal: Mapping failed unexpectedly"); // На всякий случай
        }

        // 3. Проверить смещение и записать
        if (offset >= mapping->size)
        {
            throw std::out_of_range("Server " + std::to_string(server_id) + ": Write offset " + std::to_string(offset) + " out of bounds for shm_id " + std::to_string(target_shm_id));
        }
        size_t available = mapping->size - offset;
        size_t write_len = std::min(size, available);

        memcpy(static_cast<char *>(mapping->addr) + offset, data, write_len);

        // std::cout << "Server " << server_id << " wrote " << write_len << " bytes to shm_id " << target_shm_id << " (client " << client_id << ") at offset " << offset << "." << std::endl;

        // 4. Уведомить драйвер
        u32 packed_id = pack_ids(server_id, target_shm_id);
        if (packed_id != (u32)-EINVAL)
        {
            if (ioctl(context.getFd(), IOCTL_SERVER_END_WRITING, packed_id) < 0)
            {
                perror(("Server " + std::to_string(server_id) + ": Warning - IOCTL_SERVER_END_WRITING failed for shm_id " + std::to_string(target_shm_id)).c_str());
            }
        }
        else
        {
            std::cerr << "Server " << server_id << ": Warning - Failed to pack ID for end writing notification." << std::endl;
        }

        return write_len;
    }

    size_t Server::writeToClient(int client_id, size_t offset, const std::string &text)
    {
        size_t bytes_to_write = text.length();
        size_t bytes_written = writeToClient(client_id, offset, text.c_str(), bytes_to_write);
        // Пытаемся добавить null terminator
        if (bytes_written == bytes_to_write)
        {
            int conn_idx = internal_findConnectionIndex(client_id);
            if (conn_idx != -1)
            {
                int shm_id = connections[conn_idx].shm_id;
                ServerShmMapping *mapping = internal_findOrCreateShmMapping(shm_id);
                if (mapping && mapping->mapped && (offset + bytes_written + 1) <= mapping->size)
                {
                    char nt = '\0';
                    try
                    {
                        writeToClient(client_id, offset + bytes_written, &nt, 1);
                    }
                    catch (...)
                    { /*Игнор*/
                    }
                }
            }
        }
        return bytes_written;
    }

    size_t Server::readFromSubmemory(int shm_id, size_t offset, void *buffer, size_t size_to_read)
    {
        checkInitialized();
        if (!IS_ID_VALID(shm_id))
            throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
        if (!buffer && size_to_read > 0)
            throw std::invalid_argument("Invalid buffer pointer for read.");

        // 1. Найти/создать и отобразить SHM маппинг
        ServerShmMapping *mapping = internal_findOrCreateShmMapping(shm_id);
        if (!mapping)
        {
            throw std::runtime_error("Server " + std::to_string(server_id) + ": Failed to get mapping slot for shm_id " + std::to_string(shm_id));
        }
        if (!mapping->mapped)
        {
            try
            {
                mmapSubmemory(shm_id); // Попытка mmap по требованию
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error("Server " + std::to_string(server_id) + ": Cannot read, memory shm_id " + std::to_string(shm_id) + " not mapped: " + e.what());
            }
            if (!mapping->mapped)
                throw std::logic_error("Internal: Mapping failed unexpectedly after read attempt");
        }

        // 2. Проверить смещение и прочитать
        if (offset >= mapping->size)
        {
            throw std::out_of_range("Server " + std::to_string(server_id) + ": Read offset " + std::to_string(offset) + " out of bounds for shm_id " + std::to_string(shm_id));
        }
        size_t available = mapping->size - offset;
        size_t read_len = std::min(size_to_read, available);

        if (read_len > 0)
        {
            memcpy(buffer, static_cast<const char *>(mapping->addr) + offset, read_len);
        }
        // Логирование чтения по желанию
        return read_len;
    }

    std::vector<char> Server::readFromSubmemory(int shm_id, size_t offset, size_t size_to_read)
    {
        std::vector<char> buffer(size_to_read);
        size_t bytes_read = readFromSubmemory(shm_id, offset, buffer.data(), size_to_read);
        buffer.resize(bytes_read);
        return buffer;
    }

    std::string Server::getInfo() const
    {
        std::ostringstream oss;
        oss << "  Server Name:   '" << name << "'\n";
        oss << "  Server ID:     " << (server_id == -1 ? "N/A" : std::to_string(server_id)) << "\n";
        oss << "  Initialized:   " << (initialized ? "Yes" : "No") << "\n";
        if (!initialized)
            return oss.str();

        oss << "  Connections (" << connections.size() << " slots):\n";
        int active_conn_count = 0;
        bool conn_slot_used = false;
        for (size_t i = 0; i < connections.size(); ++i)
        {
            if (connections[i].client_id != -1)
            { // Показываем только использованные слоты
                conn_slot_used = true;
                oss << "    Slot " << i << ": Client ID: " << std::left << std::setw(5) << connections[i].client_id
                    << " -> SHM ID: " << std::setw(5) << connections[i].shm_id
                    << " (Active: " << (connections[i].active ? "Yes" : "No ") << ")\n";
                if (connections[i].active)
                    active_conn_count++;
            }
        }
        if (!conn_slot_used)
            oss << "    (No connection slots used)\n";
        oss << "    (Total active connections: " << active_conn_count << ")\n";

        oss << "  SHM Mappings (" << mappings.size() << " slots):\n";
        int mapped_count = 0;
        bool map_slot_used = false;
        for (size_t i = 0; i < mappings.size(); ++i)
        {
            if (mappings[i].shm_id != -1)
            { // Показываем только использованные слоты
                map_slot_used = true;
                oss << "    Slot " << i << ": SHM ID: " << std::left << std::setw(5) << mappings[i].shm_id
                    << " -> Addr: " << std::left << std::setw(14) << (mappings[i].mapped ? mappings[i].addr : (void *)-1L)
                    << " Size: " << std::left << std::setw(6) << (mappings[i].mapped ? mappings[i].size : 0)
                    << " Mapped: " << (mappings[i].mapped ? "Yes" : "No ") << "\n";
                if (mappings[i].mapped)
                    mapped_count++;
            }
        }
        if (!map_slot_used)
            oss << "    (No mapping slots used)\n";
        oss << "    (Total mapped regions: " << mapped_count << ")\n";

        return oss.str();
    }

    // --- Обработка Уведомлений ---
    void Server::handleNotification(const notification_data &ntf)
    {
        if (!initialized || ntf.m_reciver_id != this->server_id)
            return;
        if (ntf.m_who_sends != CLIENT)
            return; // Сервер реагирует только на клиентов

        // std::cout << "[Server " << server_id << " Handler] Received notification type " << ntf.m_type
        //           << " from Client " << ntf.m_sender_id << " (SubMem: " << ntf.m_sub_mem_id << ")" << std::endl;

        switch (ntf.m_type)
        {
        case NEW_CONNECTION:
            if (IS_ID_VALID(ntf.m_sender_id) && IS_ID_VALID(ntf.m_sub_mem_id))
            {
                internal_addOrUpdateConnection(ntf.m_sender_id, ntf.m_sub_mem_id);
            }
            else
            {
                std::cerr << "Server " << server_id << ": Invalid IDs in NEW_CONNECTION." << std::endl;
            }
            break;
        case NEW_MESSAGE:
            std::cout << "Server " << server_id << ": Received NEW_MESSAGE notification from Client "
                      << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id
                      << ". Use 'server <idx> read " << ntf.m_sub_mem_id << " ...' to view." << std::endl;
            break;
        default:
            std::cout << "Server " << server_id << ": Received unhandled notification type " << ntf.m_type << std::endl;
            break;
        }
    }

    // --- Реализация приватных хелперов ---
    int Server::internal_findConnectionIndex(int client_id) const
    {
        if (!IS_ID_VALID(client_id))
            return -1;
        for (size_t i = 0; i < connections.size(); ++i)
        {
            // Ищем только активные соединения
            if (connections[i].active && connections[i].client_id == client_id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    ServerShmMapping *Server::internal_findOrCreateShmMapping(int shm_id)
    {
        if (!IS_ID_VALID(shm_id))
            return nullptr;
        int found_idx = -1;
        int first_free_idx = -1;

        for (size_t i = 0; i < mappings.size(); ++i)
        {
            if (mappings[i].shm_id == shm_id)
            { // Нашли существующий
                found_idx = i;
                break;
            }
            if (mappings[i].shm_id == -1 && first_free_idx == -1)
            { // Нашли первый свободный
                first_free_idx = i;
            }
        }

        if (found_idx != -1)
        { // Возвращаем существующий
            return &mappings[found_idx];
        }
        else if (first_free_idx != -1)
        { // Используем свободный
            mappings[first_free_idx].shm_id = shm_id;
            mappings[first_free_idx].mapped = false;
            mappings[first_free_idx].addr = MAP_FAILED;
            mappings[first_free_idx].size = 0;
            std::cout << "Server " << server_id << ": Created mapping slot for shm_id " << shm_id << " at index " << first_free_idx << "." << std::endl;
            return &mappings[first_free_idx];
        }
        else
        { // Нет места
            std::cerr << "Server " << server_id << ": No mapping slots available for shm_id " << shm_id << "." << std::endl;
            return nullptr;
        }
    }

    void Server::internal_addOrUpdateConnection(int client_id, int shm_id)
    {
        int existing_idx = internal_findConnectionIndex(client_id);

        if (existing_idx != -1)
        { // Обновляем
            if (connections[existing_idx].shm_id != shm_id)
            {
                std::cout << "Server " << server_id << ": Updating shm_id for client " << client_id << " to " << shm_id << std::endl;
                connections[existing_idx].shm_id = shm_id;
            }
            // Убеждаемся, что маппинг есть и память отображена
            ServerShmMapping *mapping = internal_findOrCreateShmMapping(shm_id);
            if (mapping && !mapping->mapped)
            {
                try
                {
                    mmapSubmemory(shm_id);
                }
                catch (...)
                {
                }
            }
            return;
        }

        // Ищем неактивный слот
        int reuse_idx = -1;
        for (size_t i = 0; i < connections.size(); ++i)
        {
            if (!connections[i].active)
            {
                reuse_idx = i;
                break;
            }
        }

        if (reuse_idx != -1)
        { // Нашли неактивный
            std::cout << "Server " << server_id << ": Adding connection client " << client_id << " -> shm " << shm_id << " to slot " << reuse_idx << std::endl;
            connections[reuse_idx].client_id = client_id;
            connections[reuse_idx].shm_id = shm_id;
            connections[reuse_idx].active = true;
            // Убеждаемся, что маппинг есть и память отображена
            ServerShmMapping *mapping = internal_findOrCreateShmMapping(shm_id);
            if (mapping && !mapping->mapped)
            {
                try
                {
                    mmapSubmemory(shm_id);
                }
                catch (...)
                {
                }
            }
        }
        else
        { // Нет места
            std::cerr << "Server " << server_id << ": No connection slots available for client " << client_id << "." << std::endl;
        }
    }

} // namespace ripc