#ifndef RIPC_TYPES_HPP
#define RIPC_TYPES_HPP

#include <functional>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

// Включаем оригинальный ripc.h для базовых структур и констант
#include "ripc.h"
#include "id_pack.h"

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
        constexpr int MAX_SERVERS = MAX_SERVERS_PER_PID;
        constexpr int MAX_SERVERS_MAPPING = 16;
        constexpr int MAX_SERVERS_CONNECTIONS = 16;
        constexpr int MAX_CLIENTS = MAX_CLIENTS_PER_PID;
    };

} // namespace ripc

#endif // RIPC_TYPES_HPP