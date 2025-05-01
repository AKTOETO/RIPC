#ifndef RIPC_TYPES_HPP
#define RIPC_TYPES_HPP

#include <functional> // std::function
#include <string>
#include <vector>
#include <cstddef> // size_t
#include <cstdint> // intptr_t

// Включаем оригинальный ripc.h для базовых структур и констант
// Путь относительно libripc/include/ripc/ до корневого include/
#include "ripc.h"
#include "id_pack.h" // Для IS_ID_VALID и т.д.

namespace ripc
{

    // --- Основные типы данных ---

    // Тип для пользовательского обработчика уведомлений
    using NotificationHandler = std::function<void(const notification_data &)>;

    // --- Вспомогательные структуры (можно вынести из Server/Client) ---

    // Информация о подключении клиента на стороне сервера
    struct ServerConnectionInfo
    {
        int client_id = -1;
        int shm_id = -1;
        bool active = false;
    };

    // Информация об отображенной submemory на стороне сервера
    struct ServerShmMapping
    {
        int shm_id = -1;
        void *addr = reinterpret_cast<void *>(-1); // MAP_FAILED
        size_t size = 0;
        bool mapped = false;
    };

    // --- Константы библиотеки (можно добавить свои) ---
    constexpr int DEFAULT_MAX_SERVERS = 16;
    constexpr int DEFAULT_MAX_CLIENTS = 128;

} // namespace ripc

#endif // RIPC_TYPES_HPP