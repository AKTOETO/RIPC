#ifndef RIPC_CONTEXT_HPP
#define RIPC_CONTEXT_HPP

#include <string>
#include <stdexcept> // std::runtime_error, std::logic_error
#include <iostream>  // Отладочный вывод

namespace ripc
{

    class RipcEntityManager; // Прямое объявление для friend

    class RipcContext
    {
    private:
        friend class RipcEntityManager; // Только менеджер создает и управляет

        int device_fd = -1;
        long page_size = 4096; // Значение по умолчанию
        std::string device_path;
        bool initialized = false;

        // Приватный конструктор, вызывается менеджером
        RipcContext() = default;

        // Запрет копирования/присваивания (управление ресурсом fd)
        RipcContext(const RipcContext &) = delete;
        RipcContext &operator=(const RipcContext &) = delete;

        // Внутренние методы инициализации/очистки, вызываемые менеджером
        void openDevice(const std::string &path);
        void closeDevice();
        void determinePageSize();

    public:
        // Деструктор закрывает устройство
        ~RipcContext();

        // Методы доступа
        int getFd() const;
        long getPageSize() const;
        bool isInitialized() const;
    };

} // namespace ripc

#endif // RIPC_CONTEXT_HPP