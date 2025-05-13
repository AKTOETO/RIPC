#include "ripc/logger.hpp"
#include "ripc/context.hpp"
#include <unistd.h> // close, sysconf
#include <fcntl.h>  // open flags
#include <stdexcept>
#include <system_error> // Для std::system_error при ошибках syscall
#include <cstring>      // strerror
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

    RipcContext::RipcContext()
        : device_fd(-1), page_size(-1), initialized(false)
    {
    }

    // Вызывается менеджером
    bool RipcContext::openDevice(const std::string &path)
    {
        if (initialized)
        {
            // throw std::logic_error("Context: Device already opened.");
            LOG_WARN("Reopening device");
        }

        device_path = path;
        device_fd = ::open(device_path.c_str(), O_RDWR | O_NONBLOCK);
        if (device_fd < 0)
        {
            int err_code = errno;
            initialized = false; // Убедимся, что флаг сброшен
            // throw std::runtime_error("Context: Failed to open device '" + device_path + "': " + strerror(err_code));
            LOG_CRIT("Failed to open device '%s': %s", device_path.c_str(), strerror(errno));
            return false;
        }
        initialized = true; // Успешно открыли
        // std::cout << "Context: Device '" << device_path << "' opened (fd=" << device_fd << ")" << std::endl;
        LOG_INFO("Device '%s' opened (fd=%d)", device_path.c_str(), device_fd);
        return determinePageSize(); // Определяем размер страницы после успешного открытия
    }

    bool RipcContext::closeDevice()
    {
        if (device_fd >= 0)
        {
            int current_fd = device_fd; // Копируем на случай, если close изменит errno
            device_fd = -1;             // Сбрасываем перед закрытием, чтобы избежать рекурсии в getFd()
            initialized = false;
            if (::close(current_fd) != 0)
            {
                // Не бросаем исключение из деструктора/closeDevice, просто логируем
                LOG_ERR("Failed to close device fd %d: %s", current_fd, strerror(errno));
                // perror(("Context: Failed to close device fd " + std::to_string(current_fd)).c_str());
                return false;
            }
            else
            {
                // std::cout << "Context: Device closed (fd=" << current_fd << ")" << std::endl;
                LOG_INFO("Device closed (fd=%d)", current_fd);
            }
        }
        initialized = false;
        return true;
    }

    bool RipcContext::determinePageSize()
    {
        errno = 0;
        long result = sysconf(_SC_PAGE_SIZE);
        if (result < 0)
        {
            if (errno != 0)
            {
                // perror("Context: sysconf(_SC_PAGE_SIZE) failed");
                LOG_WARN("sysconf(_SC_PAGE_SIZE) failed: %s", strerror(errno));
            }
            // else
            //{
            //     std::cerr << "Context: sysconf(_SC_PAGE_SIZE) returned negative value without setting errno." << std::endl;
            // }
            page_size = 4096; // Используем значение по умолчанию
            LOG_WARN("Using default page size: %d", page_size);
            // std::cerr << "Context: Using default page size: " << page_size << std::endl;
        }
        else
        {
            page_size = result;
            // std::cout << "Context: Determined page size: " << page_size << std::endl;
            LOG_INFO("Determined page size: %d", page_size);
        }
        return true;
    }

    // Деструктор
    RipcContext::~RipcContext()
    {
        closeDevice();
    }

    // Методы доступа
    int RipcContext::getFd() const
    {
        CHECK_INIT;
        if (device_fd < 0)
        {
            // throw std::logic_error("Context: Device not open or context not initialized.");
            LOG_ERR("Device in not open (fd: %d)", device_fd);
        }
        return device_fd;
    }

    long RipcContext::getPageSize() const
    {
        CHECK_INIT;
        if (page_size == -1)
        {
            LOG_ERR("Page size is not set");
        }
        return page_size;
    }

    bool RipcContext::isInitialized() const
    {
        return initialized;
    }

} // namespace ripc