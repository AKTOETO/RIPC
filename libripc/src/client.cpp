#include "ripc/logger.hpp"
#include "ripc/client.hpp"
#include "ripc/context.hpp" // Для context.getFd()
#include "ripc.h"           // IOCTL, notification_data, MAX_*
#include "id_pack.h"        // pack_ids, IS_ID_VALID
#include <sys/ioctl.h>
#include <sys/mman.h> // mmap, munmap
#include <unistd.h>   // close не нужен
#include <cstring>    // memcpy, strncpy, memset
#include <stdexcept>
#include <system_error>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace ripc
{

#define CHECK_INIT                      \
    {                                   \
        if (!isInitialized())           \
        {                               \
            LOG_ERR("Not initialized"); \
            return false;               \
        }                               \
    }
#define CHECK_CONNECTED                         \
    {                                           \
        if (!isInitialized() || !isConnected()) \
        {                                       \
            LOG_ERR("Not connected");           \
            return false;                       \
        }                                       \
    }
#define CHECK_MAPPED                                           \
    {                                                          \
        if (!isInitialized() || !isConnected() || !isMapped()) \
        {                                                      \
            LOG_ERR("Not mapped");                             \
            return false;                                      \
        }                                                      \
    }

    // Приватный конструктор
    Client::Client(RipcContext &ctx)
        : m_context(ctx), m_sub_mem(m_context),
          m_callback(nullptr), m_is_request_sent(0)
    {
        // ID будет установлен в init()
        // std::cout << "Client: Basic construction." << std::endl;
        LOG_INFO("Client: Basic construction");
    }

    // Приватный init (вызывается менеджером)
    bool Client::init()
    {
        // std::cout << "Client: init() called by EntityManager." << std::endl;
        LOG_INFO("Client: init() called by EntityManager");
        if (m_initialized)
            return false;

        int temp_client_id = -1;
        if (ioctl(m_context.getFd(), IOCTL_REGISTER_CLIENT, &temp_client_id) < 0)
        {
            LOG_CRIT("Client init failed: IOCTL_REGISTER_CLIENT failed");
            // throw std::runtime_error("Client init failed: IOCTL_REGISTER_CLIENT failed");
            return false;
        }
        if (!IS_ID_VALID(temp_client_id))
        {
            // throw std::runtime_error("Client init failed: Kernel returned invalid client_id: " + std::to_string(temp_client_id));
            LOG_CRIT("Client init failed: Kernel returned invalid client_id: %d", temp_client_id);
            return false;
        }

        // запоминаем id
        this->m_client_id = temp_client_id;

        m_initialized = true;
        // std::cout << "Client initialized with ID " << m_client_id << "." << std::endl;
        LOG_INFO("Client initialized with ID %d", m_client_id);
        return true;
    }

    // Деструктор
    Client::~Client()
    {
        LOG_INFO("Client (ID: %d destructing...", m_client_id);

        // отменяем регистрацию сущности в драйвере
        if (ioctl(m_context.getFd(), IOCTL_CLIENT_UNREGISTER, pack_ids(m_client_id, 0)) < 0)
        {
            int err_code = errno;
            // std::cout << "Client destructor failed: IOCTL_CLIENT_UNREGISTER for '"
            //           << m_client_id << "': " << strerror(err_code);
            LOG_ERR("Client destructor failed: IOCTL_CLIENT_UNREGISTER for '%d': %s", m_client_id, strerror(err_code));
        }
    }

    // // Приватные проверки
    // bool Client::checkInitialized() const
    // {
    //     if (!m_initialized)
    //         LOG_INFO("Client (ID: %d) is not initialized");
    //     return m_initialized;
    //     // throw std::logic_error("Client (ID: " + std::to_string(m_client_id) + ") is not initialized.");
    // }
    // bool Client::checkMapped() const
    // {

    //     if (!checkInitialized() || !m_sub_mem.m_is_mapped)
    //         LOG_INFO("Client`s (ID: %d) memory is not mapped");
    //     return checkInitialized() && m_sub_mem.m_is_mapped
    //     // throw std::logic_error("Client (ID: " + std::to_string(m_client_id) + ") memory not mapped.");
    // }

    bool Client::call(const Url &url, CallbackIn &&in, CallbackOut &&out)
    {
        CHECK_MAPPED

        // проверка на ответ на предыдущий запрос
        if (m_is_request_sent)
        {
            // std::cout << "Client::call: request was not sent" << std::endl;
            LOG_WARN("Cant send request: there was no response from the previous request");
            return 0;
        }

        // создаем буфер для записи
        WriteBufferView wb(m_sub_mem);

        // записываем url
        wb.addHeader(url.getUrl());

        // вызываем генератор данных для буфера
        if (out)
        {
            out(wb);
        }
        wb.finalizePayload();
        // std::cout << "Client::call: sending message '" << wb << "' to: " << url << std::endl;
        LOG_INFO("Client::call: sending message '%s' to: %s", wb.getStr().c_str(), url.getUrl().c_str());

        // уведомляем драйвер
        u32 packed_id = pack_ids(m_client_id, 0);
        if (packed_id != (u32)-EINVAL)
        {
            if (ioctl(m_context.getFd(), IOCTL_CLIENT_END_WRITING, packed_id) < 0)
            {
                LOG_ERR("Client %d: IOCTL_CLIENT_END_WRITING failed with error: %d", m_client_id, strerror(errno));
                // perror(("Client " + std::to_string(m_client_id) + ": Warning - IOCTL_CLIENT_END_WRITING failed").c_str());
            }
        }
        else
        {
            LOG_ERR("Client %d: Warning - Failed to pack ID for end writing notification.", m_client_id);
            // std::cerr << "Client " << m_client_id << ": Warning - Failed to pack ID for end writing notification." << std::endl;
        }

        // сохраняем обработчик ответа
        m_callback = in;

        // отмечаем, что запрос отправлен
        m_is_request_sent = 1;

        return false;
    }

    // --- Публичные методы ---
    int Client::getId() const
    {
        return m_client_id;
    }
    bool Client::isInitialized() const
    {
        if (!m_initialized)
            LOG_INFO("Client (ID: %d) is not initialized");
        return m_initialized;
    }
    bool Client::isConnected() const
    {
        if (m_connected_server_name.empty())
            LOG_INFO("Client (ID: %d) is not connected");
        return !m_connected_server_name.empty();
    }
    bool Client::isMapped() const
    {
        if (!m_sub_mem.m_is_mapped)
            LOG_INFO("Client`s (ID: %d) memory is not mapped");
        return m_sub_mem.m_is_mapped;
    }

    bool Client::connect(const std::string &server_name)
    {
        CHECK_INIT;

        if (server_name.empty() || server_name.length() >= MAX_SERVER_NAME)
        {
            // throw std::invalid_argument("Invalid server name for connect.");
            LOG_ERR("Invalid server name for connect operation");
            return 0;
        }

        connect_to_server connect_data;
        connect_data.client_id = this->m_client_id;
        strncpy(connect_data.server_name, server_name.c_str(), MAX_SERVER_NAME - 1);
        connect_data.server_name[MAX_SERVER_NAME - 1] = '\0';

        if (ioctl(m_context.getFd(), IOCTL_CONNECT_TO_SERVER, &connect_data) < 0)
        {
            // m_connected_server_name.clear(); // Сброс при ошибке
            // throw std::runtime_error("Client " + std::to_string(m_client_id) + ": IOCTL_CONNECT_TO_SERVER failed for '" + server_name + "'");
            LOG_ERR("Client %d: IOCTL_CONNECT_TO_SERVER failed for server '%s'", m_client_id, server_name.c_str());
            return false;
        }

        // отображаем память
        if (!m_sub_mem.m_is_mapped)
            m_sub_mem.mmap(m_client_id, 0);

        m_connected_server_name = server_name;

        LOG_INFO("Client %d: Connect request sent for server '%s'", m_client_id, server_name.c_str())
        // std::cout << "Client " << m_client_id << ": Connect request sent for server '" << server_name << "'." << std::endl;
        return true;
    }

    bool Client::disconnect()
    {
        CHECK_CONNECTED;
        auto packed_id = pack_ids(m_client_id, 0);

        if (packed_id < 0)
        {
            LOG_ERR("Client %d: cant pack id", m_client_id);
            return false;
            // throw std::runtime_error("Client::disconnect: cant pack id");
        }

        if (ioctl(m_context.getFd(), IOCTL_CLIENT_DISCONNECT, packed_id) < 0)
        {
            LOG_ERR("Client %d: failed IOCTL_CLIENT_DISCONNECT", m_client_id);
            // throw std::runtime_error("Client::disconnect: id " + std::to_string(m_client_id) +
            //                          ": failed IOCTL_CLIENT_DISCONNECT");
            return false;
        }

        // очистка полей
        m_connected_server_name.clear();
        m_sub_mem.unmap();
        return true;
    }

    std::string Client::getInfo() const
    {
        std::ostringstream oss;
        oss << "  Client ID:     " << (m_client_id == -1 ? "N/A" : std::to_string(m_client_id)) << "\n";
        oss << "  Initialized:   " << (m_initialized ? "Yes" : "No") << "\n";
        if (!m_initialized)
            return oss.str();
        oss << "  Connected to:  '" << (isConnected() ? m_connected_server_name : "(None)") << "'\n";
        oss << "  SHM Mapped:    " << (m_sub_mem.m_is_mapped ? "Yes" : "No") << "\n";
        if (m_sub_mem.m_is_mapped)
        {
            oss << "    Address:   " << m_sub_mem.m_addr << "\n";
            oss << "    Size:      " << m_sub_mem.m_max_size << " bytes\n";
        }
        return oss.str();
    }

    bool Client::handleNotification(const notification_data &ntf)
    {
        CHECK_INIT;

        if (ntf.m_reciver_id != this->m_client_id)
        {
            LOG_ERR("Client %d: incorrect receiver id %d", m_client_id, ntf.m_reciver_id);
            return false;
        }

        if (ntf.m_who_sends != SERVER)
        {
            LOG_ERR("Client %d: Got request not from server", m_client_id);
            return false;
        }

        LOG_INFO("[Client %d Handler] Received notification:", m_client_id);
        LOG_INFO("\tType: %d from server: %d", ntf.m_type, ntf.m_reciver_id);
        // std::cout << "[Client " << m_client_id << " Handler] Received notification:" << std::endl;
        // std::cout << "  Type: " << ntf.m_type << ", From Server: " << ntf.m_sender_id << std::endl;

        switch (ntf.m_type)
        {
        case NEW_MESSAGE:
            LOG_INFO("[Client %d Handler]: Received NEW_MESSAGE notification from Server: %d using SubMem %d",
                     m_client_id, ntf.m_sender_id, ntf.m_sub_mem_id)
            // std::cout << "[Client " << m_client_id
            //           << " Handler]: Received NEW_MESSAGE notification from Server: "
            //           << ntf.m_sender_id << " using SubMem " << ntf.m_sub_mem_id
            //           << std::endl;
            return dispatchNewMessage(ntf);

        case REMOTE_DISCONNECT:
            LOG_INFO("[Client %d Handler]: Received REMOTE_DISCONNECT notification from Server: %d using SubMem %d",
                     m_client_id, ntf.m_sender_id, ntf.m_sub_mem_id)
            // std::cout << "[Client " << m_client_id
            //           << " Handler]: Received REMOTE_DISCONNECT from Server: "
            //           << ntf.m_sender_id << " using SubMem " << ntf.m_sub_mem_id
            //           << std::endl;
            return dispatchRemoteDisconnect(ntf);

        default:
            std::cout << "  Action: Received unhandled notification type " << ntf.m_type << std::endl;
        }
        return false;
    }

    bool Client::dispatchNewMessage(const notification_data &ntf)
    {
        CHECK_MAPPED;
        // checkInitialized();
        // checkMapped();

        if (m_callback)
        {
            LOG_INFO("Client %d: Found callback", m_client_id);
            // std::cout << "Client::dispatchNewMessage: Found callback" << std::endl;

            // Создаем буфер для чтения из памяти
            ReadBufferView rb(m_sub_mem);

            // вызываем обработчик ответа сервера
            m_callback(rb);

            // обнуляем обработчик
            m_callback = nullptr;
        }
        else
        {
            LOG_INFO("Client %d: There is no callback", m_client_id);
            // std::cout << "Client::dispatchNewMessage: There is no callback" << std::endl;
        }
        m_is_request_sent = 0;
        return true;
    }

    bool Client::dispatchRemoteDisconnect(const notification_data &ntf)
    {
        CHECK_MAPPED;
        // checkInitialized();
        // checkMapped();

        LOG_INFO("Client %d: unmapping memory", m_client_id);
        // std::cout << "Client::dispatchRemoteDisconnect: unmapping memory\n";

        // очистка полей
        m_connected_server_name.clear();
        m_sub_mem.unmap();
        return true;
    }

} // namespace ripc
