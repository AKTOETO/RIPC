#ifndef RIPC_CONTEXT_HPP
#define RIPC_CONTEXT_HPP

#include <iostream>  // Отладочный вывод
#include <stdexcept> // std::runtime_error, std::logic_error
#include <string>

namespace ripc
{

    class RipcEntityManager; // Прямое объявление для friend

    class RipcContext
    {
      private:
        friend class RipcEntityManager; // Только менеджер создает и управляет

        int device_fd;
        long page_size; // Значение по умолчанию
        std::string device_path;
        bool initialized;

        // Приватный конструктор, вызывается менеджером
        RipcContext();

        // Запрет копирования/присваивания (управление ресурсом fd)
        RipcContext(const RipcContext &) = delete;
        RipcContext &operator=(const RipcContext &) = delete;

        // Внутренние методы инициализации/очистки, вызываемые менеджером
        bool openDevice(const std::string &path);
        bool closeDevice();
        bool determinePageSize();

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