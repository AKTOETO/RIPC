#ifndef RIPC_CLIENT_HPP
#define RIPC_CLIENT_HPP

#include "types.hpp" // Общие типы, notification_data
#include <string>
#include <vector>
#include <stdexcept> // Для исключений

// MAP_FAILED может быть не определен без sys/mman.h
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

namespace ripc
{

    class RipcContext;       // Прямое объявление
    class RipcEntityManager; // Прямое объявление

    // Класс, представляющий экземпляр клиента RIPC
    class Client
    {
    private:
        friend class RipcEntityManager; // Фабрика имеет доступ к конструктору и init

        int client_id = -1;
        RipcContext &context;     // Ссылка на общий контекст
        bool initialized = false; // Успешно ли прошел init (вызов ioctl)
        std::string connected_server_name;

        // Информация о разделяемой памяти
        void *shm_addr = MAP_FAILED;
        size_t shm_size = 0;
        bool shm_mapped = false;

        // Приватный конструктор (вызывается только RipcEntityManager)
        explicit Client(RipcContext &ctx);

        // Приватный метод инициализации (выполняет ioctl register)
        void init();

        // Приватный метод для очистки mmap
        void cleanup_shm();

        // Приватный метод для проверки состояния
        void checkInitialized() const;
        void checkMapped() const;

        // Запрет копирования/присваивания
        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;

    public:
        // Деструктор (выполняет cleanup_shm, отмена регистрации в ядре делается менеджером)
        ~Client();

        // --- Основные операции ---
        void connect(const std::string &server_name);
        void mmap();
        size_t write(size_t offset, const void *data, size_t size);
        size_t write(size_t offset, const std::string &text);
        size_t read(size_t offset, void *buffer, size_t size_to_read);
        std::vector<char> read(size_t offset, size_t size_to_read);

        // --- Получение информации ---
        int getId() const;
        bool isInitialized() const; // Проверяет успешность init()
        bool isConnected() const;
        bool isMapped() const;
        std::string getInfo() const; // Форматированная строка для вывода

        // --- Обработка уведомлений ---
        // Вызывается менеджером, если нет пользовательского обработчика
        void handleNotification(const notification_data &ntf);
    };

} // namespace ripc

#endif // RIPC_CLIENT_HPP