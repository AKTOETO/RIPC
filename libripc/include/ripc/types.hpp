#ifndef RIPC_TYPES_HPP
#define RIPC_TYPES_HPP

#include <functional> // std::function
#include <string>
#include <vector>
#include <cstddef> // size_t
#include <cstdint> // intptr_t

// Включаем оригинальный ripc.h для базовых структур и констант
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