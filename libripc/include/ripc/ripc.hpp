#ifndef RIPC_API_HPP
#define RIPC_API_HPP

#include "logger.hpp"
#include "types.hpp"
#include "server.hpp"
#include "client.hpp"
#include <string>
#include <cstddef>

// --- Публичный интерфейс библиотеки RIPC ---

namespace ripc
{

    // Прямые объявления классов (пользователь работает с указателями)
    class Server;
    class Client;

    // --- Управление библиотекой ---

    /**
     * @brief Инициализирует библиотеку и соединение с драйвером RIPC.
     * Должна быть вызвана один раз перед использованием любых других функций библиотеки.
     * @param device_path Путь к файлу устройства драйвера (например, "/dev/ripc").
     * @throws std::runtime_error если инициализация не удалась.
     */
    bool initialize(const std::string &device_path = DEVICE_PATH); // DEVICE_PATH из ripc.h

    /**
     * @brief Завершает работу библиотеки, освобождает все ресурсы.
     * Вызывает деструкторы всех созданных клиентов и серверов.
     */
    bool shutdown();

    // --- Создание и удаление сущностей ---

    /**
     * @brief Создает и регистрирует новый экземпляр сервера.
     * @param name Имя сервера (макс. MAX_SERVER_NAME - 1 символов).
     * @return Невладеющий указатель на созданный объект Server.
     * @throws std::runtime_error если достигнут лимит серверов или регистрация не удалась.
     * @throws std::invalid_argument если имя некорректно.
     */
    Server *createServer(const std::string &name);

    /**
     * @brief Создает и регистрирует новый экземпляр клиента.
     * @return Невладеющий указатель на созданный объект Client.
     * @throws std::runtime_error если достигнут лимит клиентов или регистрация не удалась.
     */
    Client *createClient();

    /**
     * @brief Удаляет экземпляр сервера по его ID ядра.
     * Вызывает деструктор объекта Server и освобождает связанные ресурсы.
     * @param server_id ID сервера, полученный при создании.
     * @return true, если сервер был найден и удален, иначе false.
     */
    bool deleteServer(int server_id);

    /**
     * @brief Удаляет экземпляр сервера.
     * Вызывает деструктор объекта Server и освобождает связанные ресурсы.
     * @param server Указатель на объект сервера.
     * @return true, если сервер был найден и удален, иначе false.
     */
    bool deleteServer(Server *server);

    /**
     * @brief Удаляет экземпляр клиента по его ID ядра.
     * Вызывает деструктор объекта Client и освобождает связанные ресурсы.
     * @param client_id ID клиента, полученный при создании.
     * @return true, если клиент был найден и удален, иначе false.
     */
    bool deleteClient(int client_id);

    /**
     * @brief Удаляет экземпляр клиента.
     * Вызывает деструктор объекта Client и освобождает связанные ресурсы.
     * @param client Указатель на объект клиента, который надо удалить
     */
    bool deleteClient(Client *client);

    // --- Поиск сущностей ---

    /**
     * @brief Находит существующий экземпляр сервера по его ID ядра.
     * @param server_id ID искомого сервера.
     * @return Невладеющий указатель на объект Server или nullptr, если не найден.
     */
    Server *findServerById(int server_id);

    /**
     * @brief Находит существующий экземпляр клиента по его ID ядра.
     * @param client_id ID искомого клиента.
     * @return Невладеющий указатель на объект Client или nullptr, если не найден.
     */
    Client *findClientById(int client_id);

    // --- Обработка уведомлений ---

    /**
     * @brief Регистрирует пользовательскую функцию-обработчик для указанного типа уведомлений.
     * Если обработчик зарегистрирован, он будет вызван ВМЕСТО стандартной обработки
     * (вызова метода handleNotification у соответствующего Client/Server).
     * @param type Тип уведомления (из enum notif_type в ripc.h).
     * @param handler Функция или лямбда вида void(const notification_data&).
     *                Передача пустой функции (nullptr или {}) отменяет регистрацию.
     */
    bool registerNotificationHandler(enum notif_type type, NotificationHandler handler);

    // --- Функции API для управления логгированием ---
    /**
     * @brief Устанавливает минимальный уровень логгирования.
     * Сообщения с уровнем ниже указанного не будут выводиться/обрабатываться.
     * @param level Минимальный уровень логгирования.
     */
    void setLogLevel(LogLevel level);

    /**
     * @brief Устанавливает пользовательский поток для вывода логов.
     * По умолчанию используется std::cerr.
     * @param os Указатель на поток вывода. Если nullptr, используется std::cerr.
     */
    void setLogStream(std::ostream *os);

    /**
     * @brief Устанавливает поведение при критических ошибках (уровень CRITICAL).
     * @param behavior THROW_EXCEPTION (бросать CriticalLogError) или LOG_ONLY.
     */
    void setCriticalLogBehavior(Logger::CriticalBehavior behavior);

} // namespace ripc

#endif // RIPC_API_HPP