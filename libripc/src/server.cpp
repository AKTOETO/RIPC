#include "ripc/logger.hpp"
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
    // нулевая память
    const std::pair<const int, std::shared_ptr<Memory>> Server::ConnectionInfo::m_null_submem{-1, nullptr};

#define CHECK_INIT                      \
    {                                   \
        if (!isInitialized())           \
        {                               \
            LOG_ERR("Not initialized"); \
            return false;               \
        }                               \
    }
#define CHECK_INIT_R(val)               \
    {                                   \
        if (!isInitialized())           \
        {                               \
            LOG_ERR("Not initialized"); \
            return val;                 \
        }                               \
    }

    // Приватный конструктор
    Server::Server(RipcContext &ctx, const std::string &server_name)
        : m_context(ctx), m_name(server_name), m_server_id(-1),
          m_connections{}, m_initialized(false),
          m_mappings(DEFAULTS::MAX_SERVERS_MAPPING)
    {
        m_connections.reserve(DEFAULTS::MAX_SERVERS_CONNECTIONS);
        if (m_name.empty() || m_name.length() >= MAX_SERVER_NAME)
        {
            // throw std::invalid_argument("Invalid server name.");
            LOG_WARN("Invalid arguments");
            return;
        }
        // std::cout << "Server '" << m_name << "': Basic construction." << std::endl;
        LOG_INFO("Server's basic constructor");
    }

    // Приватный init
    bool Server::init()
    {
        if (m_initialized)
            return true;
        // std::cout << "Server '" << m_name << "': init() called by EntityManager." << std::endl;
        LOG_INFO("Server '%s': init() called by EntityManager", m_name.c_str());

        server_registration reg_data;
        strncpy(reg_data.name, m_name.c_str(), MAX_SERVER_NAME - 1);
        reg_data.name[MAX_SERVER_NAME - 1] = '\0';
        reg_data.server_id = -1;

        if (ioctl(m_context.getFd(), IOCTL_REGISTER_SERVER, &reg_data) < 0)
        {
            int err_code = errno;
            // throw std::runtime_error("Server init failed: IOCTL_REGISTER_SERVER for '" +
            //                          m_name + "': " + strerror(err_code));
            LOG_CRIT("failed: IOCTL_REGISTER_SERVER for '%s': %s ", m_name.c_str(), strerror(err_code));
            return false;
        }
        if (!IS_ID_VALID(reg_data.server_id))
        {
            LOG_CRIT("failed: returned invalid server_id = %d", reg_data.server_id);
            return false;
            // throw std::runtime_error("Server init failed: Kernel returned invalid server_id: " +
            //                          std::to_string(reg_data.server_id));
        }

        this->m_server_id = reg_data.server_id;
        m_initialized = true;
        // std::cout << "Server '" << m_name << "' initialized with ID " << m_server_id << "." << std::endl;
        LOG_INFO("Server '%s' initialized with ID %d", m_name.c_str(), m_server_id);
        return true;
    }

    // Деструктор
    Server::~Server()
    {
        // std::cout << "Server '" << m_name << "' (ID: " << m_server_id << ") destructing..." << std::endl;
        LOG_INFO("Server '%s' (ID: %d) destructing...", m_name.c_str(), m_server_id);

        // отменяем регистрацию сущности в драйвере
        if (ioctl(m_context.getFd(), IOCTL_SERVER_UNREGISTER, pack_ids(m_server_id, 0)) < 0)
        {
            int err_code = errno;
            // std::cout << "Server init failed: IOCTL_SERVER_UNREGISTER for '"
            //           << m_name << "': " << strerror(err_code);
            LOG_ERR("Server init failed: IOCTL_SERVER_UNREGISTER for '%s': %s", m_name.c_str(), strerror(err_code));
        }
    }

    // Приватные проверки
    // void Server::checkInitialized() const
    //{
    //    if (!m_initialized)
    //        throw std::logic_error("Server '" + m_name + "' (ID: " +
    //                               std::to_string(m_server_id) + ") is not initialized.");
    //}

    // --- Публичные методы ---
    int Server::getId() const { return m_server_id; }
    const std::string &Server::getName() const { return m_name; }
    bool Server::isInitialized() const { return m_initialized; }

    bool Server::writeToClient(std::shared_ptr<ConnectionInfo> con, WriteBufferView &result)
    {
        LOG_INFO("sending answer to client");
        // std::cout << "Server::WriteToClient: sending answer to client" << std::endl;
        // checkInitialized();
        CHECK_INIT;
        if (!con)
        {
            LOG_ERR("empty connection ptr");
            return false;
            // throw std::invalid_argument("Server::writeToClient: empty connection ptr");
        }

        // память, куда будем писать
        auto &mem = con->m_sub_mem_p;
        if (!mem.second)
        {
            LOG_ERR("empty mem ptr");
            return false;
        }
        // throw std::runtime_error("Server::writeToClient: empty mem ptr");
        if (!mem.second->m_is_mapped)
        {
            LOG_ERR("mem is not mapped");
            return false;
        }
        // throw std::runtime_error("Server::writeToClient: mem is not mapped");

        // пишем в память данные
        // size_t write_len = mem.second->write(0, result.data(), result.getCurrentSize());

        // отправляем уведомление
        u32 packed_id = pack_ids(m_server_id, mem.first);
        if (packed_id != (u32)-EINVAL)
        {
            if (ioctl(m_context.getFd(), IOCTL_SERVER_END_WRITING, packed_id) < 0)
            {
                LOG_ERR("Server '%s' IOCTL_SERVER_END_WRITING failed for shm_id %d", m_name.c_str(), mem.first);
                return false;
                // perror(("Server " + std::to_string(m_server_id) +
                //         ": Warning - IOCTL_SERVER_END_WRITING failed for shm_id " + std::to_string(mem.first))
                //            .c_str());
            }
        }
        else
        {
            LOG_ERR("Failed to pack id");
            // std::cerr << "Server " << m_server_id << ": Warning - Failed to pack ID for end writing notification." << std::endl;
        }
        return true;
    }

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

    bool Server::registerCallback(UrlPattern &&url_pattern, UrlCallbackFull &&callback)
    {
        auto [it, inserted] = m_urls.try_emplace(std::move(url_pattern), std::move(callback));

        if (inserted)
            // std::cout << "Server::registerCallback: callback to '" << it->first << "' registered\n";
            LOG_INFO("callback to '%s' registered", it->first.getUrl().c_str())

        else
            // std::cout << "Server::registerCallback: callback to '" << it->first << "' NOT registered\n";
            LOG_WARN("callback to '%s' registered", it->first.getUrl().c_str())
        return inserted; // registerCallback(std::move(url_pattern), std::move(callback.m_in), std::move(callback.m_out));
    }

    bool Server::registerCallback(UrlPattern &&url_pattern, UrlCallbackIn &&in, UrlCallbackOut &&out)
    {
        return registerCallback(std::move(url_pattern), std::move(UrlCallbackFull{in, out}));

        // auto [it, inserted] = m_urls.try_emplace(std::move(url_pattern), std::move(UrlCallbackFull{in, out}));
        // if (inserted)
        //     std::cout << "Server::registerCallback: callback to '" << it->first << "' registered\n";
        // else
        //     std::cout << "Server::registerCallback: callback to '" << it->first << "' NOT registered\n";
        // return inserted;
    }

    // --- Обработка Уведомлений ---
    bool Server::handleNotification(const notification_data &ntf)
    {
        // checkInitialized();
        CHECK_INIT;
        if (ntf.m_reciver_id != this->m_server_id)
            return false;
        if (ntf.m_who_sends != CLIENT)
            return false;
        if (!IS_ID_VALID(ntf.m_sender_id) || !IS_ID_VALID(ntf.m_sub_mem_id))
            return false;

        LOG_INFO("[Server %d Handler]: Received notification type %d from Client %d SubMem id: %d)",
                 m_server_id, ntf.m_type, ntf.m_sender_id, ntf.m_sub_mem_id);
        // std::cout << "[Server " << m_server_id << " Handler] Received notification type " << ntf.m_type
        //           << " from Client " << ntf.m_sender_id << " (SubMem id: " << ntf.m_sub_mem_id << ")" << std::endl;

        switch (ntf.m_type)
        {
        case NEW_CONNECTION:
            // std::cout << "[Server " << m_server_id
            //           << " Handler]: Received NEW_CONNECTION notification from Client "
            //           << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id << std::endl;
            LOG_INFO("[Server %d Handler]: Received NEW_CONNECTION from Client %d SubMem id: %d)",
                     m_server_id, ntf.m_type, ntf.m_sender_id, ntf.m_sub_mem_id);
            return addConnection(ntf.m_sender_id, ntf.m_sub_mem_id);
            break;

        case NEW_MESSAGE:
            // std::cout << "[Server " << m_server_id
            //           << " Handler]: Received NEW_MESSAGE notification from Client "
            //           << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id
            //           << std::endl;
            LOG_INFO("[Server %d Handler]: Received NEW_MESSAGE from Client %d SubMem id: %d)",
                     m_server_id, ntf.m_type, ntf.m_sender_id, ntf.m_sub_mem_id);
            // вызваем обработчик нового сообщения
            return dispatchNewMessage(ntf);
            break;

        case REMOTE_DISCONNECT:
            // std::cout << "[Server " << m_server_id
            //           << " Handler]: Received REMOTE_DISCONNECT notification from Client "
            //           << ntf.m_sender_id << " regarding SubMem " << ntf.m_sub_mem_id
            //           << std::endl;
            LOG_INFO("[Server %d Handler]: Received REMOTE_DISCONNECT from Client %d SubMem id: %d)",
                     m_server_id, ntf.m_type, ntf.m_sender_id, ntf.m_sub_mem_id);
            return disconnectFromClient(findConnection(ntf.m_sender_id));
            break;
        default:
            LOG_ERR("Server %d Received unhandled notification type %d", m_server_id, ntf.m_type);
            // std::cout << "Server " << m_server_id << ": Received unhandled notification type " << ntf.m_type << std::endl;
        }
        return false;
    }

    bool Server::dispatchNewMessage(const notification_data &ntf)
    {
        // checkInitialized();
        CHECK_INIT;

        // поиск нужного соединения
        auto con = findConnection(ntf.m_sender_id);
        if (!con)
        {
            // throw std::logic_error(
            //    "Server::dispatchNewMessage: doesnt have conection to client: " + std::to_string(ntf.m_sender_id));
            LOG_ERR("Server %d doesnt have conection to client: %d", m_server_id, ntf.m_sender_id);
            return false;
        }
        auto &mem = con->m_sub_mem_p;

        // создаем ReadBuffer для чтения из памяти
        ReadBufferView rb(*mem.second);

        // читаем URL
        auto url_str = rb.getHeader();

        // если url нет, тогда игнорируем это запрос
        if (!url_str)
        {
            // std::cerr << "Server::dispatchNewMessage: there is no URL in the memory\n";
            LOG_ERR("there is no URL in the memory");
            return false;
        }

        // создаем URl из строки
        Url url(*url_str);
        // std::cout << "Server::dispatchNewMessage: url: [" << url << "]\n";
        LOG_INFO("url: %s", url.getUrl().c_str());

        // создаем выходной буфер
        WriteBufferView wb(*mem.second);

        // ищем подходящий обработчик для этого url
        bool found_callback = false;
        for (auto &[pattern, callback_struct] : m_urls)
        {
            if (pattern == url)
            {
                found_callback = 1;
                // обрабатываем входящий запрос
                if (callback_struct.m_in)
                {
                    // std::cout << "Server::dispatchNewMessage: calling input callback" << std::endl;
                    LOG_INFO("Calling input callback");
                    callback_struct.m_in(url, rb);
                }

                // генерируем ответные данные
                if (callback_struct.m_out)
                {
                    LOG_INFO("Calling output callback");
                    // std::cout << "Server::dispatchNewMessage: calling output callback" << std::endl;
                    callback_struct.m_out(wb);
                    wb.finalizePayload();
                }

                // отправляем ответ клиенту
                return writeToClient(con, wb);
                break;
            }
        }

        if (!found_callback)
            LOG_ERR("There is no callback for url: %s", *url_str);
        // std::cerr << "[Server::dispatchNewMessage] There is no callback for url: " << *url_str << std::endl;
        return false;
    }

    // отключение клиента от сервера
    bool Server::disconnectFromClient(std::shared_ptr<ConnectionInfo> con)
    {
        // std::cout << "Server::disconnectFromClient: disconnecting client\n";
        LOG_INFO("disconnecting from client")
        // checkInitialized();
        CHECK_INIT;

        if (!con)
        {
            LOG_ERR("Unable to disconnect NULL connection");
            return false;
            // throw std::runtime_error("Server::disconnectFromClient: Unable to disconnect NULL connection");
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

        return true;
    }

    // --- Реализация приватных хелперов ---
    // поиск соединения
    std::shared_ptr<Server::ConnectionInfo> Server::findConnection(int client_id) const
    {
        // checkInitialized();
        CHECK_INIT_R(nullptr);

        if (!IS_ID_VALID(client_id))
        {
            // throw std::invalid_argument("Invalid client_id: " + std::to_string(client_id));
            LOG_ERR("Invalid client_id: %s", client_id);
            return nullptr;
        }

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
        // checkInitialized();
        CHECK_INIT_R(Server::ConnectionInfo::m_null_submem);

        if (!IS_ID_VALID(shm_id))
        {
            // throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
            LOG_ERR("Invalid shm_id: %d", shm_id);
            return Server::ConnectionInfo::m_null_submem;
        }

        if (m_mappings.size() >= DEFAULTS::MAX_SERVERS_MAPPING)
        {
            // throw std::out_of_range("Not enough space for one more mapping in server: id: " + std::to_string(m_server_id));
            LOG_ERR("Not enough space for one more mapping in server_id: %d", m_server_id);
            return Server::ConnectionInfo::m_null_submem;
        }

        // пытаемся добавить новую память
        auto [it, inserted] = m_mappings.try_emplace(shm_id, std::make_shared<Memory>(m_context));
        if (!inserted)
        {
            LOG_INFO("SubMem with id: %d already exist", shm_id);
            return *it;
            // throw std::out_of_range("SubMem with id: " + std::to_string(shm_id) + " already exist");
        }

        // std::cout << "Server " << m_server_id << ": Created mapping slot for shm_id " << shm_id << std::endl;
        LOG_INFO("Server %d: Created mapping slot for shm_id %d", m_server_id, shm_id);
        return *it;
    }

    bool Server::addConnection(int client_id, int shm_id)
    {
        // checkInitialized();
        CHECK_INIT

        // проверка id
        if (!IS_ID_VALID(shm_id))
        {
            // throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
            LOG_ERR("Invalid shm_id: %d", shm_id);
            return false;
        }
        if (!IS_ID_VALID(client_id))
        {
            // throw std::invalid_argument("Invalid shm_id: " + std::to_string(shm_id));
            LOG_ERR("Invalid client_id: %d", client_id);
            return false;
        }

        auto conn = findConnection(client_id);

        // проверка на существование соединения
        if (conn)
        {
            // std::cerr << "Connection already exists. server id: "
            //           << std::to_string(m_server_id) << " client_id: "
            //           << std::to_string(client_id)
            //           << std::endl;
            LOG_ERR("Connection already exists. server id: %d client_id: %d ", m_server_id, client_id);
            return false;
        }

        // проверка на количество соединений в сервере
        if (m_connections.size() >= DEFAULTS::MAX_SERVERS_CONNECTIONS)
        {
            // std::cerr << "Server::addConnection: Too many connections in server id: "
            //           << std::to_string(m_server_id)
            //           << ". Cant create one more.\n";
            LOG_ERR("Too many connections in server id: %d", m_server_id);
            return false;
        }

        // получаем память для этого соединения
        auto &map = findOrCreateSHM(shm_id);

        // если нет такой памяти
        if(map == Server::ConnectionInfo::m_null_submem)
        {
            
            return false;
        }

        // отображаем память, если не отображена
        if (!map.second->m_is_mapped)
        {
            map.second->mmap(m_server_id, shm_id);
        }

        // создаем соединение
        m_connections.emplace_back(std::make_shared<ConnectionInfo>(client_id, map));

        // std::cout << "Server " << m_server_id
        //           << ": Adding connection client " << client_id
        //           << " -> shm " << shm_id << std::endl;
        LOG_INFO("Server %d: Adding connection client %d -> shm %d", m_server_id, client_id, shm_id);
        return true;
    }

} // namespace ripc