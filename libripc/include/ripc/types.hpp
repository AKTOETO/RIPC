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

// Если не определен map failed
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

namespace ripc
{

    // --- Основные типы данных ---

    // Тип для пользовательского обработчика уведомлений
    using NotificationHandler = std::function<void(const notification_data &)>;

    // --- Вспомогательные структуры (можно вынести из Server/Client) ---

    // Информация о подключении клиента на стороне сервера
    //struct ServerConnectionInfo
    //{
    //    int client_id = -1;
    //    int shm_id = -1;
    //    bool active = false;
    //};

    // Информация об отображенной submemory на стороне сервера
    //struct ServerShmMapping
    //{
    //    int shm_id = -1;
    //    void *addr = MAP_FAILED; //reinterpret_cast<void *>(-1);
    //    size_t size = 0;
    //    bool mapped = false;
    //};

    // --- Константы библиотеки ---
    namespace DEFAULTS
    {
        constexpr int MAX_SERVERS = 16;
        constexpr int MAX_SERVERS_MAPPING = 16;
        constexpr int MAX_SERVERS_CONNECTIONS = 16;
        constexpr int MAX_CLIENTS = 128;
    };

} // namespace ripc

#endif // RIPC_TYPES_HPP