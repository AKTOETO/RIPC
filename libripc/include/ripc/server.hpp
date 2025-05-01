#ifndef RIPC_SERVER_HPP
#define RIPC_SERVER_HPP

#include "types.hpp" // Общие типы, notification_data, структуры Info/Mapping
#include <string>
#include <vector>
#include <stdexcept> // Для исключений

namespace ripc
{

    class RipcContext;       // Прямое объявление
    class RipcEntityManager; // Прямое объявление

    // Класс, представляющий экземпляр сервера RIPC
    class Server
    {
    private:
        friend class RipcEntityManager; // Фабрика имеет доступ

        int server_id = -1;
        std::string name;
        RipcContext &context;
        bool initialized = false;

        // Используем типы, определенные в ripc_types.hpp
        std::vector<ServerConnectionInfo> connections;
        std::vector<ServerShmMapping> mappings;

        // Приватный конструктор
        Server(RipcContext &ctx, const std::string &server_name);

        // Приватный метод инициализации (ioctl register)
        void init();

        // Приватный метод очистки mmap
        void cleanup_mappings();

        // Приватные хелперы для управления соединениями/маппингами
        void internal_addOrUpdateConnection(int client_id, int shm_id);
        int internal_findConnectionIndex(int client_id) const;
        ServerShmMapping *internal_findOrCreateShmMapping(int shm_id);

        // Приватный метод для проверки состояния
        void checkInitialized() const;

        // Запрет копирования/присваивания
        Server(const Server &) = delete;
        Server &operator=(const Server &) = delete;

    public:
        // Деструктор (выполняет cleanup_mappings)
        ~Server();

        // --- Основные операции ---
        void mmapSubmemory(int shm_id);
        size_t writeToClient(int client_id, size_t offset, const void *data, size_t size);
        size_t writeToClient(int client_id, size_t offset, const std::string &text);
        size_t readFromSubmemory(int shm_id, size_t offset, void *buffer, size_t size_to_read);
        std::vector<char> readFromSubmemory(int shm_id, size_t offset, size_t size_to_read);

        // --- Получение информации ---
        int getId() const;
        const std::string &getName() const;
        bool isInitialized() const;
        std::string getInfo() const; // Форматированная строка

        // --- Обработка уведомлений ---
        void handleNotification(const notification_data &ntf);
    };

} // namespace ripc

#endif // RIPC_SERVER_HPP