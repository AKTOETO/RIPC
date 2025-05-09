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
#include <tuple>

namespace ripc
{

    // Приватный конструктор
    Server::Server(RipcContext &ctx, const std::string &server_name)
        : m_context(ctx), m_name(server_name),
          m_connections{},
          m_mappings(DEFAULTS::MAX_SERVERS_MAPPING)
    {
        m_connections.reserve(DEFAULTS::MAX_SERVERS_CONNECTIONS);
        if (m_name.empty() || m_name.length() >= MAX_SERVER_NAME)
        {
            throw std::invalid_argument("Invalid server name.");
        }
        std::cout << "Server '" << m_name << "': Basic construction." << std::endl;
    }

    // Приватный init
    void Server::init()
    {
        if (m_initialized)
            return;
        std::cout << "Server '" << m_name << "': init() called by EntityManager." << std::endl;

        server_registration reg_data;
        strncpy(reg_data.name, m_name.c_str(), MAX_SERVER_NAME - 1);
        reg_data.name[MAX_SERVER_NAME - 1] = '\0';
        reg_data.server_id = -1;

        if (ioctl(m_context.getFd(), IOCTL_REGISTER_SERVER, &reg_data) < 0)
        {
            int err_code = errno;
            throw std::runtime_error("Server init failed: IOCTL_REGISTER_SERVER for '" +
                                     m_name + "': " + strerror(err_code));
        }
        if (!IS_ID_VALID(reg_data.server_id))
        {
            throw std::runtime_error("Server init failed: Kernel returned invalid server_id: " +
                                     std::to_string(reg_data.server_id));
        }

        this->m_server_id = reg_data.server_id;
        m_initialized = true;
        std::cout << "Server '" << m_name << "' initialized with ID " << m_server_id << "." << std::endl;
    }

    // Деструктор
    Server::~Server()
    {
        std::cout << "Server '" << m_name << "' (ID: " << m_server_id << ") destructing..." << std::endl;
        // cleanup_mappings();

        // TODO:
        //  Отмена регистрации в драйвере должна быть тут
    }

    // Приватная очистка mmap
    // void Server::cleanup_mappings()
    // {
    //     int unmap_count = 0;
    //     for (auto &[id, map_info] : mappings)
    //     {
    //         if (map_info.mapped && map_info.addr != MAP_FAILED)
    //         {
    //             void *current_addr = map_info.addr;
    //             size_t current_size = map_info.size;

    //             map_info.mapped = false;
    //             map_info.addr = MAP_FAILED;
    //             map_info.size = 0;

    //             if (munmap(current_addr, current_size) == 0)
    //             {
    //                 unmap_count++;
    //             }
    //             else
    //             {
    //                 perror(("Server " + std::to_string(server_id) +
    //                         ": munmap failed for shm_id " + std::to_string(id))
    //                            .c_str());
    //             }
    //         }
    //         // Сбрасываем неактивные слоты тоже на всякий случай
    //     }
    //     if (unmap_count > 0)
    //     {
    //         std::cout << "Server " << server_id << ": Unmapped " << unmap_count << " regions." << std::endl;
    //     }
    //     // // Очищаем вектор или сбрасываем shm_id? Зависит от логики findOrCreate
    //     // for (auto &map_info : mappings)
    //     //     map_info.shm_id = -1; // Сбросим ID для чистоты
    //     mappings.clear();
    // }

    // Приватные проверки
    void Server::checkInitialized() const
    {
        if (!m_initialized)
            throw std::logic_error("Server '" + m_name + "' (ID: " +
                                   std::to_string(m_server_id) + ") is not initialized.");
    }

    // --- Публичные методы ---
    int Server::getId() const { return m_server_id; }
    const std::string &Server::getName() const { return m_name; }
    bool Server::isInitialized() const { return m_initialized; }

    // void Server::mmapSubmemory(int shm_id)
    // {
    //     checkInitialized();
    //     if (!IS_ID_VALID(shm_id))
    //         throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));

    //     auto mapping = findOrCreateSHM(shm_id);
    //     if (!mapping)
    //     {
    //         // findOrCreateSHM уже вывел ошибку, если не смог создать
    //         throw std::runtime_error("Server " + std::to_string(server_id) + ": Failed to get mapping slot for shm_id " + std::to_string(shm_id));
    //     }

    //     if (mapping->second.mapped)
    //     {
    //         // std::cout << "Server " << server_id << ": shm_id " << shm_id << " already mapped." << std::endl;
    //         return;
    //     }

    //     u32 packed_id = pack_ids(server_id, shm_id);
    //     if (packed_id == (u32)-EINVAL)
    //         throw std::logic_error("Failed to pack IDs for mmap.");

    //     off_t offset = (off_t)packed_id * context.getPageSize();
    //     std::cout << "Server " << server_id << ": Attempting mmap for shm_id " << shm_id
    //               << " (offset 0x" << std::hex << offset << ", packed 0x" << packed_id << ")" << std::dec << std::endl;

    //     void *addr = ::mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, context.getFd(), offset);

    //     if (addr == MAP_FAILED)
    //     {
    //         int err_code = errno;
    //         throw std::runtime_error("Server " + std::to_string(server_id) +
    //                                  ": mmap failed for shm_id " + std::to_string(shm_id) + ": " + strerror(err_code));
    //     }

    //     mapping->addr = addr;
    //     mapping->size = SHM_REGION_PAGE_SIZE;
    //     mapping->mapped = true;
    //     std::cout << "Server " << server_id << ": Mapped shm_id " << shm_id << " at " << addr << std::endl;
    // }

    size_t Server::writeToClient(std::shared_ptr<ConnectionInfo> con, Buffer &&result)
    {
        std::cout << "Server::WriteToClient: sending answer to client" << std::endl;
        checkInitialized();
        if (!con)
            throw std::invalid_argument("Server::writeToClient: empty connection ptr");

        // память, куда будем писать
        auto &mem = con->m_sub_mem_p;
        if (!mem.second)
            throw std::runtime_error("Server::writeToClient: empty mem ptr");
        if (!mem.second->m_is_mapped)
            throw std::runtime_error("Server::writeToClient: mem is not mapped");

        // пишем в память данные
        size_t write_len = mem.second->write(0, result.data(), result.size());

        // отправляем уведомление
        u32 packed_id = pack_ids(m_server_id, mem.first);
        if (packed_id != (u32)-EINVAL)
        {
            if (ioctl(m_context.getFd(), IOCTL_SERVER_END_WRITING, packed_id) < 0)
            {
                perror(("Server " + std::to_string(m_server_id) +
                        ": Warning - IOCTL_SERVER_END_WRITING failed for shm_id " + std::to_string(mem.first))
                           .c_str());
            }
        }
        else
        {
            std::cerr << "Server " << m_server_id << ": Warning - Failed to pack ID for end writing notification." << std::endl;
        }

        return write_len;
    }

    // size_t Server::writeToClient(int client_id, size_t offset, const void *data, size_t size)
    // {
    //     checkInitialized();
    //     if (!IS_ID_VALID(client_id))
    //         throw std::invalid_argument("Invalid client_id: " + std::to_string(client_id));
    //     if (!data)
    //         throw std::invalid_argument("Invalid data pointer for write.");

    //     // 1. Найти активное соединение
    //     int conn_idx = findConnection(client_id);
    //     if (conn_idx == -1)
    //     {
    //         throw std::runtime_error("Server " + std::to_string(m_server_id) +
    //                                  ": No active connection found for client ID " + std::to_string(client_id));
    //     }
    //     int target_shm_id = m_connections[conn_idx].shm_id;

    //     // 2. Найти/создать и отобразить SHM маппинг
    //     ServerShmMapping *mapping = findOrCreateSHM(target_shm_id);
    //     if (!mapping)
    //     {
    //         throw std::runtime_error("Server " + std::to_string(m_server_id) + ": Failed to get mapping slot for shm_id " + std::to_string(target_shm_id));
    //     }
    //     if (!mapping->mapped)
    //     {
    //         try
    //         {
    //             mmapSubmemory(target_shm_id); // Попытка mmap по требованию
    //         }
    //         catch (const std::exception &e)
    //         {
    //             throw std::runtime_error("Server " + std::to_string(m_server_id) + ": Cannot write, memory shm_id " + std::to_string(target_shm_id) + " not mapped: " + e.what());
    //         }
    //         if (!mapping->mapped)
    //             throw std::logic_error("Internal: Mapping failed unexpectedly"); // На всякий случай
    //     }

    //     // 3. Проверить смещение и записать
    //     if (offset >= mapping->size)
    //     {
    //         throw std::out_of_range("Server " + std::to_string(m_server_id) + ": Write offset " + std::to_string(offset) + " out of bounds for shm_id " + std::to_string(target_shm_id));
    //     }
    //     size_t available = mapping->size - offset;
    //     size_t write_len = std::min(size, available);

    //     memcpy(static_cast<char *>(mapping->addr) + offset, data, write_len);

    //     // std::cout << "Server " << server_id << " wrote " << write_len << " bytes to shm_id " << target_shm_id << " (client " << client_id << ") at offset " << offset << "." << std::endl;

    //     // 4. Уведомить драйвер
    //     u32 packed_id = pack_ids(m_server_id, target_shm_id);
    //     if (packed_id != (u32)-EINVAL)
    //     {
    //         if (ioctl(m_context.getFd(), IOCTL_SERVER_END_WRITING, packed_id) < 0)
    //         {
    //             perror(("Server " + std::to_string(m_server_id) + ": Warning - IOCTL_SERVER_END_WRITING failed for shm_id " + std::to_string(target_shm_id)).c_str());
    //         }
    //     }
    //     else
    //     {
    //         std::cerr << "Server " << m_server_id << ": Warning - Failed to pack ID for end writing notification." << std::endl;
    //     }

    //     return write_len;
    // }

    // size_t Server::writeToClient(int client_id, size_t offset, const std::string &text)
    // {
    //     size_t bytes_to_write = text.length();
    //     size_t bytes_written = writeToClient(client_id, offset, text.c_str(), bytes_to_write);
    //     // Пытаемся добавить null terminator
    //     if (bytes_written == bytes_to_write)
    //     {
    //         int conn_idx = findConnection(client_id);
    //         if (conn_idx != -1)
    //         {
    //             int shm_id = m_connections[conn_idx].shm_id;
    //             ServerShmMapping *mapping = findOrCreateSHM(shm_id);
    //             if (mapping && mapping->mapped && (offset + bytes_written + 1) <= mapping->size)
    //             {
    //                 char nt = '\0';
    //                 try
    //                 {
    //                     writeToClient(client_id, offset + bytes_written, &nt, 1);
    //                 }
    //                 catch (...)
    //                 { /*Игнор*/
    //                 }
    //             }
    //         }
    //     }
    //     return bytes_written;
    // }

    // size_t Server::readFromSubmemory(int shm_id, size_t offset, void *buffer, size_t size_to_read)
    // {
    //     checkInitialized();
    //     if (!IS_ID_VALID(shm_id))
    //         throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
    //     if (!buffer)
    //         throw std::invalid_argument("Invalid buffer pointer for read.");

    //     // 1. Найти/создать и отобразить SHM маппинг
    //     ServerShmMapping *mapping = findOrCreateSHM(shm_id);
    //     if (!mapping)
    //     {
    //         throw std::runtime_error("Server " + std::to_string(m_server_id) + ": Failed to get mapping slot for shm_id " + std::to_string(shm_id));
    //     }
    //     if (!mapping->mapped)
    //     {
    //         try
    //         {
    //             mmapSubmemory(shm_id); // Попытка mmap по требованию
    //         }
    //         catch (const std::exception &e)
    //         {
    //             throw std::runtime_error("Server " + std::to_string(m_server_id) + ": Cannot read, memory shm_id " + std::to_string(shm_id) + " not mapped: " + e.what());
    //         }
    //         if (!mapping->mapped)
    //             throw std::logic_error("Internal: Mapping failed unexpectedly after read attempt");
    //     }

    //     // 2. Проверить смещение и прочитать
    //     if (offset >= mapping->size)
    //     {
    //         throw std::out_of_range("Server " + std::to_string(m_server_id) + ": Read offset " + std::to_string(offset) + " out of bounds for shm_id " + std::to_string(shm_id));
    //     }
    //     size_t available = mapping->size - offset;
    //     size_t read_len = std::min(size_to_read, available);

    //     if (read_len > 0)
    //     {
    //         memcpy(buffer, static_cast<const char *>(mapping->addr) + offset, read_len);
    //     }
    //     // Логирование чтения по желанию
    //     return read_len;
    // }

    // std::vector<char> Server::readFromSubmemory(int shm_id, size_t offset, size_t size_to_read)
    // {
    //     std::vector<char> buffer(size_to_read);
    //     size_t bytes_read = readFromSubmemory(shm_id, offset, buffer.data(), size_to_read);
    //     buffer.resize(bytes_read);
    //     return buffer;
    // }

    std::string Server::getInfo() const
    {
        std::ostringstream oss;
        oss << "  Server Name:   '" << m_name << "'\n";
        oss << "  Server ID:     " << (m_server_id == -1 ? "N/A" : std::to_string(m_server_id)) << "\n";
        oss << "  Initialized:   " << (m_initialized ? "Yes" : "No") << "\n";
        if (!m_initialized)
            return oss.str();

        oss << "  Connections (" << m_connections.size() << " slots):\n";
        int active_conn_count = 0;
        bool conn_slot_used = false;
        for (size_t i = 0; i < m_connections.size(); ++i)
        {
            if (m_connections[i]->client_id != -1)
            { // Показываем только использованные слоты
                conn_slot_used = true;
                oss << "    Slot " << i << ": Client ID: " << std::left << std::setw(5) << m_connections[i]->client_id
                    << " -> SHM ID: " << std::setw(5) << m_connections[i]->m_sub_mem_p.first
                    << " (Active: " << (m_connections[i]->active ? "Yes" : "No ") << ")\n";
                if (m_connections[i]->active)
                    active_conn_count++;
            }
        }
        if (!conn_slot_used)
            oss << "    (No connection slots used)\n";
        oss << "    (Total active connections: " << active_conn_count << ")\n";

        oss << "  SHM Mappings (" << m_mappings.size() << " slots):\n";
        int mapped_count = 0;
        bool map_slot_used = false;
        int i = 0;
        for (auto &[id, submem] : m_mappings)
        {
            if (id != -1)
            { // Показываем только использованные слоты
                map_slot_used = true;
                oss << "    Slot " << i++ << ": SHM ID: " << std::left << std::setw(5) << id
                    << " -> Addr: " << std::left << std::setw(14) << (submem->m_is_mapped ? submem->m_addr : (void *)-1L)
                    << " Size: " << std::left << std::setw(6) << (submem->m_is_mapped ? submem->m_max_size : 0)
                    << " Mapped: " << (submem->m_is_mapped ? "Yes" : "No ") << "\n";
                if (submem->m_is_mapped)
                    mapped_count++;
            }
        }
        if (!map_slot_used)
            oss << "    (No mapping slots used)\n";
        oss << "    (Total mapped regions: " << mapped_count << ")\n";

        return oss.str();
    }

    // bool Server::registerCallback(const UrlPattern& url_pattern, UrlCallback &&callback)
    // {
    //     auto [it, inserted] = m_urls.try_emplace(url_pattern, std::move(callback));
    //     if (inserted)
    //         std::cout << "Server::registerCallback: callback to '" << url_pattern << "' registered\n";
    //     else
    //         std::cout << "Server::registerCallback: callback to '" << url_pattern << "' NOT registered\n";
    //     return inserted;
    // }

    // bool Server::registerCallback(const std::string &url_pattern, UrlCallback &&callback)
    // {
    //     auto [it, inserted] = m_urls.try_emplace(

    //         url_pattern,
    //         std::forward_as_tuple(std::forward<UrlCallback>(callback))  // Перемещаем callback
    //     );
    //     if (inserted)
    //         std::cout << "Server::registerCallback: callback to '" << url_pattern << "' registered\n";
    //     else
    //         std::cout << "Server::registerCallback: callback to '" << url_pattern << "' NOT registered\n";
    //     return inserted;
    // }

    // bool Server::registerCallback(const char *url_pattern, UrlCallback &&callback)
    // {
    //     auto [it, inserted] = m_urls.try_emplace(UrlPattern(url_pattern), std::move(callback));
    //     if (inserted)
    //         std::cout << "Server::registerCallback: callback to '" << url_pattern << "' registered\n";
    //     else
    //         std::cout << "Server::registerCallback: callback to '" << url_pattern << "' NOT registered\n";
    //     return inserted;
    // }

    bool Server::registerCallback(UrlPattern &&url_pattern, UrlCallback &&callback)
    {
        auto [it, inserted] = m_urls.try_emplace(std::move(url_pattern), std::move(callback));

        if (inserted)
            std::cout << "Server::registerCallback: callback to '" << it->first << "' registered\n";
        else
            std::cout << "Server::registerCallback: callback to '" << it->first << "' NOT registered\n";
        return inserted;
    }

    // --- Обработка Уведомлений ---
    void Server::handleNotification(const notification_data &ntf)
    {
        checkInitialized();
        if (ntf.m_reciver_id != this->m_server_id)
            return;
        if (ntf.m_who_sends != CLIENT)
            return;
        if (!IS_ID_VALID(ntf.m_sender_id) || !IS_ID_VALID(ntf.m_sub_mem_id))
            return;

        std::cout << "[Server " << m_server_id << " Handler] Received notification type " << ntf.m_type
                  << " from Client " << ntf.m_sender_id << " (SubMem id: " << ntf.m_sub_mem_id << ")" << std::endl;

        switch (ntf.m_type)
        {
        case NEW_CONNECTION:
            std::cout << "[Server " << m_server_id
                      << " Handler]: Received NEW_CONNECTION notification from Client "
                      << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id << std::endl;
            addConnection(ntf.m_sender_id, ntf.m_sub_mem_id);
            break;

        case NEW_MESSAGE:
            std::cout << "[Server " << m_server_id
                      << " Handler]: Received NEW_MESSAGE notification from Client "
                      << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id
                      << std::endl;
            // вызваем обработчик нового сообщения
            dispatchNewMessage(ntf);
            break;

        case REMOTE_DISCONNECT:
            std::cout << "[Server " << m_server_id
                      << " Handler]: Received REMOTE_DISCONNECT notification from Client "
                      << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id
                      << std::endl;
            disconnectFromClient(findConnection(ntf.m_sender_id));
            break;
        default:
            std::cout << "Server " << m_server_id << ": Received unhandled notification type " << ntf.m_type << std::endl;
            break;
        }
    }

    void Server::dispatchNewMessage(const notification_data &ntf)
    {
        checkInitialized();

        // поиск нужного соединения
        auto con = findConnection(ntf.m_sender_id);
        if (!con)
            throw std::logic_error(
                "Server::dispatchNewMessage: doesnt have conection to client: " + std::to_string(ntf.m_sender_id));
        auto &mem = con->m_sub_mem_p;

        // читаем в буфер url
        size_t url_size = 100;
        char url_str[100] = {};
        size_t read_pos = 0;

        // копируем url
        if ((read_pos = mem.second->readUntil(0, url_str, url_size - 1, '\0')) == 0)
        {
            throw std::runtime_error("Server::dispatchNewMessage: there is not URL in the message");
        }
        url_str[read_pos - 1] = '\0';

        // создаем url
        Url url(url_str);
        std::cout << "Server::dispatchNewMessage: url: [" << url << "]\n";

        // копируем оставшееся послание
        // Buffer buf(mem.second->read(read_pos));
        Buffer buf(*mem.second, read_pos);
        std::cout << "Server::dispatchNewMessage: last part of buffer: [" << buf << "]\n";

        //  Создаем выходной буфер
        Buffer out;

        // ищем подходящий обработчик для этого url
        bool found_callback = false;
        for (auto &[pattern, callback] : m_urls)
        {
            if (pattern == url)
            {
                // вычисляем ответ
                std::cout << "Server::dispatchNewMessage: calling the callback" << std::endl;
                callback(ntf, buf, out);

                // отправляем ответ клиенту
                writeToClient(con, std::move(out));
                found_callback = 1;
                break;
            }
        }

        if (!found_callback)
            std::cerr << "[Server::dispatchNewMessage] There is no callback for url: " << url_str << std::endl;
    }

    // отключение клиента от сервера
    void Server::disconnectFromClient(std::shared_ptr<ConnectionInfo> con)
    {
        std::cout<<"Server::disconnectFromClient: disconnecting client\n";
        checkInitialized();

        if (!con)
        {
            throw std::runtime_error("Server::disconnectFromClient: Unable to disconnect NULL connection");
        }

        // удалить ячейку памяти
        m_mappings.erase(con->m_sub_mem_p.first);

        // удалить соединениеs
        auto it = std::find_if(
            m_connections.begin(), m_connections.end(),
            [&con](const auto &el)
            {
                return el->client_id == con->client_id;
            });

        m_connections.erase(it);
    }

    // --- Реализация приватных хелперов ---
    // поиск соединения
    std::shared_ptr<Server::ConnectionInfo> Server::findConnection(int client_id) const
    {
        checkInitialized();

        if (!IS_ID_VALID(client_id))
            throw std::invalid_argument("Invalid client_id: " + std::to_string(client_id));

        for (auto &el : m_connections)
        {
            if (el && el->active && el->client_id == client_id)
            {
                return el;
            }
        }

        return nullptr;
    }

    // поиск или создание общей памяти
    const std::pair<const int, std::shared_ptr<Memory>> &Server::findOrCreateSHM(int shm_id)
    {
        checkInitialized();

        if (!IS_ID_VALID(shm_id))
        {
            throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
            // std::cerr << "Invalid shm_id: " << shm_id << std::endl;
            // return std::nullopt;
        }

        if (m_mappings.size() >= DEFAULTS::MAX_SERVERS_MAPPING)
        {
            throw std::out_of_range("Not enough space for one more mapping in server: id: " + std::to_string(m_server_id));
            // std::cerr << "Not enough space for one more mapping in server: id: " << server_id << std::endl;
            // return std::nullopt;
        }

        // пытаемся добавить новую память
        auto [it, inserted] = m_mappings.try_emplace(shm_id, std::make_shared<Memory>(m_context));
        if (!inserted)
            throw std::out_of_range("SubMem with id: " + std::to_string(shm_id) + " already exist");

        std::cout << "Server " << m_server_id << ": Created mapping slot for shm_id " << shm_id << std::endl;
        return *it;
    }

    void Server::addConnection(int client_id, int shm_id)
    {
        checkInitialized();

        // проверка id
        if (!IS_ID_VALID(shm_id))
            throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
        if (!IS_ID_VALID(client_id))
            throw std::invalid_argument("Invalid client_id: " + std::to_string(client_id));

        auto conn = findConnection(client_id);

        // проверка на существование соединения
        if (conn)
        {
            std::cerr << "Connection already exists. server id: "
                      << std::to_string(m_server_id) << " client_id: "
                      << std::to_string(client_id)
                      << std::endl;
            return;
        }

        // проверка на количество соединений в сервере
        if (m_connections.size() >= DEFAULTS::MAX_SERVERS_CONNECTIONS)
        {
            std::cerr << "Server::addConnection: Too many connections in server id: "
                      << std::to_string(m_server_id)
                      << ". Cant create one more.\n";
            return;
        }

        // получаем память для этого соединения
        auto &map = findOrCreateSHM(shm_id);

        // отображаем память, если не отображена
        if (!map.second->m_is_mapped)
        {
            map.second->mmap(m_server_id, shm_id);
            // mmapSubmemory(shm_id);
        }

        // создаем соединение
        m_connections.emplace_back(std::make_shared<ConnectionInfo>(client_id, map));

        std::cout << "Server " << m_server_id
                  << ": Adding connection client " << client_id
                  << " -> shm " << shm_id << std::endl;
    }

} // namespace ripc