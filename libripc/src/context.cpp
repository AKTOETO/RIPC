#include "ripc/context.hpp"
#include <unistd.h> // close, sysconf
#include <fcntl.h>  // open flags
#include <stdexcept>
#include <system_error> // Для std::system_error при ошибках syscall
#include <cstring>      // strerror
#include <iostream>

namespace ripc
{

    // Вызывается менеджером
    void RipcContext::openDevice(const std::string &path)
    {
        if (initialized)
        {
            throw std::logic_error("Context: Device already opened.");
        }
        device_path = path;
        // O_NONBLOCK важен для poll/read в потоке менеджера
        device_fd = ::open(device_path.c_str(), O_RDWR | O_NONBLOCK);
        if (device_fd < 0)
        {
            int err_code = errno;
            initialized = false; // Убедимся, что флаг сброшен
            throw std::runtime_error("Context: Failed to open device '" + device_path + "': " + strerror(err_code));
        }
        initialized = true; // Успешно открыли
        std::cout << "Context: Device '" << device_path << "' opened (fd=" << device_fd << ")" << std::endl;
        determinePageSize(); // Определяем размер страницы после успешного открытия
    }

    void RipcContext::closeDevice()
    {
        if (device_fd >= 0)
        {
            int current_fd = device_fd; // Копируем на случай, если close изменит errno
            device_fd = -1;             // Сбрасываем перед закрытием, чтобы избежать рекурсии в getFd()
            initialized = false;
            if (::close(current_fd) != 0)
            {
                // Не бросаем исключение из деструктора/closeDevice, просто логируем
                perror(("Context: Failed to close device fd " + std::to_string(current_fd)).c_str());
            }
            else
            {
                std::cout << "Context: Device closed (fd=" << current_fd << ")" << std::endl;
            }
        }
        initialized = false; // Сбрасываем в любом случае
    }

    void RipcContext::determinePageSize()
    {
        errno = 0; // Сброс перед sysconf
        long result = sysconf(_SC_PAGE_SIZE);
        if (result < 0)
        {
            if (errno != 0)
            {
                perror("Context: sysconf(_SC_PAGE_SIZE) failed");
            }
            else
            {
                std::cerr << "Context: sysconf(_SC_PAGE_SIZE) returned negative value without setting errno." << std::endl;
            }
            page_size = 4096; // Используем значение по умолчанию
            std::cerr << "Context: Using default page size: " << page_size << std::endl;
        }
        else
        {
            page_size = result;
            std::cout << "Context: Determined page size: " << page_size << std::endl;
        }
    }

    // Деструктор
    RipcContext::~RipcContext()
    {
        closeDevice();
    }

    // Методы доступа
    int RipcContext::getFd() const
    {
        if (!initialized || device_fd < 0)
        {
            throw std::logic_error("Context: Device not open or context not initialized.");
        }
        return device_fd;
    }

    long RipcContext::getPageSize() const
    {
        if (!initialized)
        {
            throw std::logic_error("Context: Page size not determined or context not initialized.");
        }
        return page_size;
    }

    bool RipcContext::isInitialized() const
    {
        return initialized;
    }

} // namespace ripc