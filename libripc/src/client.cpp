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

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

namespace ripc
{

    // Приватный конструктор
    Client::Client(RipcContext &ctx) : context(ctx)
    {
        // ID будет установлен в init()
        std::cout << "Client: Basic construction." << std::endl;
    }

    // Приватный init (вызывается менеджером)
    void Client::init()
    {
        if (initialized)
            return;
        std::cout << "Client: init() called by EntityManager." << std::endl;

        int temp_client_id = -1;
        if (ioctl(context.getFd(), IOCTL_REGISTER_CLIENT, &temp_client_id) < 0)
        {
            throw std::runtime_error("Client init failed: IOCTL_REGISTER_CLIENT failed");
        }
        if (!IS_ID_VALID(temp_client_id))
        {
            throw std::runtime_error("Client init failed: Kernel returned invalid client_id: " + std::to_string(temp_client_id));
        }

        this->client_id = temp_client_id;
        initialized = true;
        std::cout << "Client initialized with ID " << client_id << "." << std::endl;
    }

    // Деструктор
    Client::~Client()
    {
        // std::cout << "Client (ID: " << client_id << ") destructing..." << std::endl; // Отладка
        cleanup_shm();
        // Отмена регистрации в ядре (unregister ioctl) должна происходить
        // из RipcEntityManager::deleteClient, чтобы гарантировать,
        // что контекст (и fd) еще жив на момент вызова ioctl.
    }

    // Приватная очистка mmap
    void Client::cleanup_shm()
    {
        if (shm_mapped && shm_addr != MAP_FAILED)
        {
            int current_id = client_id; // Копируем ID для лога
            void *current_addr = shm_addr;
            size_t current_size = shm_size;

            // Сбрасываем флаги ДО вызова munmap, чтобы избежать гонок или рекурсии
            shm_mapped = false;
            shm_addr = MAP_FAILED;
            shm_size = 0;

            if (munmap(current_addr, current_size) != 0)
            {
                perror(("Client " + std::to_string(current_id) + ": munmap failed").c_str());
            }
            else
            {
                std::cout << "Client " << current_id << ": Unmapped memory at " << current_addr << std::endl;
            }
        }
        // Сбрасываем еще раз на всякий случай
        shm_addr = MAP_FAILED;
        shm_size = 0;
        shm_mapped = false;
    }

    // Приватные проверки
    void Client::checkInitialized() const
    {
        if (!initialized)
            throw std::logic_error("Client (ID: " + std::to_string(client_id) + ") is not initialized.");
    }
    void Client::checkMapped() const
    {
        checkInitialized();
        if (!shm_mapped)
            throw std::logic_error("Client (ID: " + std::to_string(client_id) + ") memory not mapped.");
    }

    // --- Публичные методы ---
    int Client::getId() const { return client_id; }
    bool Client::isInitialized() const { return initialized; }
    bool Client::isConnected() const { return !connected_server_name.empty(); }
    bool Client::isMapped() const { return shm_mapped; }

    void Client::connect(const std::string &server_name)
    {
        checkInitialized();
        if (server_name.empty() || server_name.length() >= MAX_SERVER_NAME)
        {
            throw std::invalid_argument("Invalid server name for connect.");
        }

        connect_to_server connect_data;
        connect_data.client_id = this->client_id;
        strncpy(connect_data.server_name, server_name.c_str(), MAX_SERVER_NAME - 1);
        connect_data.server_name[MAX_SERVER_NAME - 1] = '\0';

        if (ioctl(context.getFd(), IOCTL_CONNECT_TO_SERVER, &connect_data) < 0)
        {
            connected_server_name.clear(); // Сброс при ошибке
            throw std::runtime_error("Client " + std::to_string(client_id) + ": IOCTL_CONNECT_TO_SERVER failed for '" + server_name + "'");
        }

        connected_server_name = server_name;
        std::cout << "Client " << client_id << ": Connect request sent for server '" << server_name << "'." << std::endl;
    }

    void Client::mmap()
    {
        checkInitialized();
        if (shm_mapped)
        {
            std::cout << "Client " << client_id << ": Memory already mapped." << std::endl;
            return;
        }

        u32 packed_id = pack_ids(client_id, 0); // shm_id для клиента = 0
        if (packed_id == (u32)-EINVAL)
        {
            throw std::logic_error("Client " + std::to_string(client_id) + ": Failed to pack ID for mmap.");
        }

        off_t offset = (off_t)packed_id * context.getPageSize();
        std::cout << "Client " << client_id << ": Attempting mmap with offset 0x"
                  << std::hex << offset << " (packed 0x" << packed_id << ")" << std::dec << std::endl;

        void *addr = ::mmap(NULL, SHM_REGION_PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, context.getFd(), offset);

        if (addr == MAP_FAILED)
        {
            int err_code = errno;
            throw std::runtime_error("Client " + std::to_string(client_id) + ": mmap failed: " + strerror(err_code));
        }

        shm_addr = addr;
        shm_size = SHM_REGION_PAGE_SIZE;
        shm_mapped = true;
        std::cout << "Client " << client_id << ": Memory mapped at " << shm_addr << std::endl;
    }

    size_t Client::write(size_t offset, const void *data, size_t size)
    {
        checkMapped();
        if (!data && size > 0)
            throw std::invalid_argument("Invalid data pointer for write.");

        if (offset >= shm_size)
        {
            throw std::out_of_range("Client " + std::to_string(client_id) + ": Write offset " + std::to_string(offset) + " out of bounds (" + std::to_string(shm_size) + ")");
        }

        size_t available = shm_size - offset;
        size_t write_len = std::min(size, available);

        memcpy(static_cast<char *>(shm_addr) + offset, data, write_len);

        // std::cout << "Client " << client_id << " wrote " << write_len << " bytes at offset " << offset << "." << std::endl;

        // Уведомляем драйвер, только если подключены
        if (isConnected())
        {
            u32 packed_id = pack_ids(client_id, 0);
            if (packed_id != (u32)-EINVAL)
            {
                if (ioctl(context.getFd(), IOCTL_CLIENT_END_WRITING, packed_id) < 0)
                {
                    // Логируем, но не бросаем исключение - запись прошла успешно
                    perror(("Client " + std::to_string(client_id) + ": Warning - IOCTL_CLIENT_END_WRITING failed").c_str());
                }
            }
            else
            {
                std::cerr << "Client " << client_id << ": Warning - Failed to pack ID for end writing notification." << std::endl;
            }
        }

        return write_len;
    }

    size_t Client::write(size_t offset, const std::string &text)
    {
        // +1 для возможного null terminator
        size_t bytes_to_write = text.length();
        size_t bytes_written = write(offset, text.c_str(), bytes_to_write);

        // Попытка записать null terminator, если все байты строки влезли и есть место
        if (bytes_written == bytes_to_write && (offset + bytes_written + 1) <= shm_size)
        {
            char nt = '\0';
            try
            {
                write(offset + bytes_written, &nt, 1); // Может вызвать второе уведомление
            }
            catch (...)
            { /* Игнорируем ошибку записи терминатора */
            }
        }
        return bytes_written; // Возвращаем кол-во байт строки
    }

    size_t Client::read(size_t offset, void *buffer, size_t size_to_read)
    {
        checkMapped(); // Проверяет и инициализацию, и маппинг
        if (!buffer && size_to_read > 0)
            throw std::invalid_argument("Invalid buffer pointer for read.");

        if (offset >= shm_size)
        {
            throw std::out_of_range("Client " + std::to_string(client_id) + ": Read offset " + std::to_string(offset) + " out of bounds (" + std::to_string(shm_size) + ")");
        }

        size_t available = shm_size - offset;
        size_t read_len = std::min(size_to_read, available);

        if (read_len > 0)
        {
            memcpy(buffer, static_cast<const char *>(shm_addr) + offset, read_len);
        }
        // Логирование чтения можно добавить по желанию
        return read_len;
    }

    std::vector<char> Client::read(size_t offset, size_t size_to_read)
    {
        std::vector<char> buffer(size_to_read);
        size_t bytes_read = read(offset, buffer.data(), size_to_read);
        buffer.resize(bytes_read); // Оставляем только прочитанные байты
        return buffer;
    }

    std::string Client::getInfo() const
    {
        std::ostringstream oss;
        oss << "  Client ID:     " << (client_id == -1 ? "N/A" : std::to_string(client_id)) << "\n";
        oss << "  Initialized:   " << (initialized ? "Yes" : "No") << "\n";
        if (!initialized)
            return oss.str();
        oss << "  Connected to:  '" << (isConnected() ? connected_server_name : "(None)") << "'\n";
        oss << "  SHM Mapped:    " << (shm_mapped ? "Yes" : "No") << "\n";
        if (shm_mapped)
        {
            oss << "    Address:   " << shm_addr << "\n";
            oss << "    Size:      " << shm_size << " bytes\n";
        }
        return oss.str();
    }

    void Client::handleNotification(const notification_data &ntf)
    {
        if (!initialized || ntf.m_reciver_id != this->client_id)
            return;
        if (ntf.m_who_sends != SERVER)
            return; // Клиент обрабатывает только от сервера

        std::cout << "[Client " << client_id << " Handler] Received notification:" << std::endl;
        std::cout << "  Type: " << ntf.m_type << ", From Server: " << ntf.m_sender_id << std::endl;

        switch (ntf.m_type)
        {
        case NEW_MESSAGE:
            std::cout << "  Action: New message available. Use 'client <idx> read ...' to view." << std::endl;
            // TODO: добавить колбэк на передачу ответа выше
            char buf[100];
            buf[99] = '\0';
            read(0, buf, 98);
            std::cout << "  New message: " << buf << std::endl;
            break;
        default:
            std::cout << "  Action: Received unhandled notification type " << ntf.m_type << std::endl;
            break;
        }
    }

} // namespace ripc