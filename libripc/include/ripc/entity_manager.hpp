#ifndef RIPC_ENTITY_MANAGER_HPP
#define RIPC_ENTITY_MANAGER_HPP

#include "context.hpp" // Менеджер владеет контекстом
#include "ripc/rest_client.hpp"
#include "ripc/rest_server.hpp"
#include "types.hpp"   // Для NotificationHandler и других общих типов
#include <atomic>      // std::atomic<bool> для флага потока
#include <map>         // Используем для карты обработчиков (enum -> handler)
#include <memory>      // std::unique_ptr
#include <mutex>       // std::mutex, std::lock_guard
#include <string>
#include <thread>        // std::thread для слушателя
#include <unordered_map> // Используем для быстрого поиска по ID

// Прямые объявления зависимых классов, чтобы избежать включения их заголовков здесь
namespace ripc
{
    class Server;
    class Client;
} // namespace ripc

namespace ripc
{

    // Синглтон Менеджер и Фабрика для Клиентов и Серверов
    class RipcEntityManager
    {
      private:
        // --- Данные ---
        std::unique_ptr<RipcContext> context; // Владеет единственным экземпляром контекста
        // Карты для хранения и владения объектами (ID -> Умный указатель)
        std::unordered_map<int, std::unique_ptr<Server>> servers;
        std::unordered_map<int, std::unique_ptr<Client>> clients;
        // Карта для пользовательских обработчиков уведомлений
        std::map<enum notif_type, NotificationHandler> notification_handlers;

        // Синхронизация
        std::mutex manager_mutex; // Мьютекс для защиты карт (servers, clients, notification_handlers)

        // Лимиты
        size_t max_servers = DEFAULTS::MAX_SERVERS; // Используем константу из ripc_types.hpp
        size_t max_clients = DEFAULTS::MAX_CLIENTS; // Используем константу из ripc_types.hpp

        // Состояние инициализации
        bool is_initialized = false;

        // Компоненты для потока-слушателя
        std::thread listener_thread;
        std::atomic<bool> listener_running{false}; // Атомарный флаг для управления потоком

        // --- Приватные методы ---

        // Приватный конструктор для реализации синглтона
        RipcEntityManager() = default;

        // Запрет копирования и присваивания синглтона
        RipcEntityManager(const RipcEntityManager &) = delete;
        RipcEntityManager &operator=(const RipcEntityManager &) = delete;

        // Внутренние методы инициализации и завершения (вызываются из глобальных функций)
        friend bool initialize(const std::string &device_path);
        friend bool shutdown();
        bool doInitialize(const std::string &device_path);
        bool doShutdown();

        // Основная логика потока-слушателя
        bool notificationListenerLoop();

        // Диспетчеризация полученных уведомлений
        bool dispatchNotification(const notification_data &ntf);

        // проверка инициализации
        bool isInitialized() const;

      public:
        ~RipcEntityManager();
        // --- Доступ к синглтону ---

        /**
         * @brief Возвращает единственный экземпляр RipcEntityManager.
         * @throws std::logic_error если библиотека не была инициализирована вызовом ripc::initialize().
         * @return Ссылка на экземпляр менеджера.
         */
        static RipcEntityManager &getInstance();

        // --- Фабричные методы ---

        /**
         * @brief Создает, инициализирует и регистрирует новый экземпляр сервера.
         * Вызывает приватный конструктор и init() сервера.
         * @param name Имя нового сервера.
         * @return Невладеющий указатель на созданный объект Server. Управление жизнью объекта остается у менеджера.
         * @throws std::runtime_error если достигнут лимит серверов или произошла ошибка при регистрации в ядре.
         * @throws std::invalid_argument если имя сервера некорректно.
         * @throws std::logic_error если менеджер не инициализирован.
         */
        Server *createServer(const std::string &name);

        /**
         * @brief Создает, инициализирует и регистрирует новый экземпляр Restfull сервера.
         * Вызывает приватный конструктор и init() сервера.
         * @return Невладеющий указатель на созданный объект RESTServer. Управление жизнью объекта остается у менеджера.
         */
        RESTServer* createRestfulServer(const std::string &name);

        /**
         * @brief Создает, инициализирует и регистрирует новый экземпляр клиента.
         * Вызывает приватный конструктор и init() клиента.
         * @return Невладеющий указатель на созданный объект Client. Управление жизнью объекта остается у менеджера.
         * @throws std::runtime_error если достигнут лимит клиентов или произошла ошибка при регистрации в ядре.
         * @throws std::logic_error если менеджер не инициализирован.
         */
        Client *createClient();

        /**
         * @brief Создает, инициализирует и регистрирует новый экземпляр Restfull клиента.
         * Вызывает приватный конструктор и init() клиента.
         * @return Невладеющий указатель на созданный объект RESTClient. Управление жизнью объекта остается у менеджера.
         */
        RESTClient* createRestfulClient();

        // --- Удаление сущностей ---

        /**
         * @brief Удаляет сервер по его ID ядра.
         * Находит сервер в карте, вызывает его деструктор (через unique_ptr) и удаляет из карты.
         * @param server_id ID сервера для удаления.
         * @return true, если сервер был найден и удален, иначе false.
         */
        bool deleteServer(int server_id);

        /**
         * @brief Удаляет сервер.
         * Находит сервер в карте, вызывает его деструктор (через unique_ptr) и удаляет из карты.
         * @param server Указатель на объект сервера.
         * @return true, если сервер был найден и удален, иначе false.
         */
        bool deleteServer(Server *server);

        /**
         * @brief Удаляет клиента по его ID ядра.
         * Находит клиента в карте, вызывает его деструктор (через unique_ptr) и удаляет из карты.
         * @param client_id ID клиента для удаления.
         * @return true, если клиент был найден и удален, иначе false.
         */
        bool deleteClient(int client_id);

        /**
         * @brief Удаляет клиента.
         * Находит клиента в карте, вызывает его деструктор (через unique_ptr) и удаляет из карты.
         * @param client Указатель на объект клиента.
         * @return true, если клиент был найден и удален, иначе false.
         */
        bool deleteClient(Client *client);

        // --- Поиск сущностей ---

        /**
         * @brief Находит сервер по его ID ядра.
         * @param server_id ID искомого сервера.
         * @return Невладеющий указатель на найденный Server или nullptr, если не найден.
         */
        Server *findServerById(int server_id);

        /**
         * @brief Находит клиента по его ID ядра.
         * @param client_id ID искомого клиента.
         * @return Невладеющий указатель на найденный Client или nullptr, если не найден.
         */
        Client *findClientById(int client_id);

        // --- Доступ к контексту ---

        /**
         * @brief Предоставляет доступ к общему контексту библиотеки (содержит fd и т.д.).
         * @return Ссылка на объект RipcContext.
         * @throws std::logic_error если менеджер или контекст не инициализированы.
         */
        RipcContext &getContext();

        // --- Управление обработчиками уведомлений ---

        /**
         * @brief Регистрирует пользовательскую функцию-обработчик для заданного типа уведомлений.
         * Этот обработчик будет вызван *вместо* стандартной логики (вызова handleNotification объекта).
         * Повторный вызов для того же типа переопределит предыдущий обработчик.
         * Передача пустой функции (nullptr или {}) отменяет регистрацию.
         * @param type Тип уведомления (enum notif_type из ripc.h).
         * @param handler Функция-обработчик (или лямбда).
         * @throws std::invalid_argument если тип уведомления некорректен.
         */
        bool registerHandler(enum notif_type type, NotificationHandler handler);
    };

} // namespace ripc

#endif // RIPC_ENTITY_MANAGER_HPP