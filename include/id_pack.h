#ifndef ID_PACK_H
#define ID_PACK_H

// --- Определение типов в зависимости от окружения ---

#ifdef __KERNEL__
// --- Код для Ядра Linux ---
#include <linux/types.h> // Определяет u16, u32
#include <linux/errno.h>
#include "err.h"
#else
// --- Код для Пользовательского пространства ---
#include <stdint.h> // Стандарт C99 для типов фиксированного размера
#include <stdio.h>  // Для fprintf в примере проверки (можно убрать если не нужно)
#include <stdlib.h> // Для exit в примере проверки (можно убрать если не нужно)
#include <errno.h>  // для кодов ошибок

// Определяем псевдонимы для совместимости имен с ядром
typedef uint16_t u16;
typedef uint32_t u32;

#undef __FUNCTION__
#define __FUNCTION__ __func__

// макрос для вывода ошибок, если нужно.
#undef pr_err
#define ERR(fmt, ...) \
    fprintf(stderr, "ERROR: %s :" fmt "\n", __FUNCTION__, ##__VA_ARGS__)

#endif // __KERNEL__

/**
 * Запаковка и распаковка id для передачи между процессом и дарйвером
 */

// Максимальное значение для ID (2 байта = 16 бит = 65535)
#define MAX_ID_VALUE (u16)0xFFFF // (1 << 16) - 1

// Проверка на верность значения id
#define IS_ID_VALID(id) !((u32)id > MAX_ID_VALUE || id < 0)

/**
 * @brief Упаковывает два 16-битных ID в один 32-битный unsigned int.
 * @param id1 Первый ID (старшие 16 бит). Должен быть <= MAX_ID_VALUE.
 * @param id2 Второй ID (младшие 16 бит). Должен быть <= MAX_ID_VALUE.
 * @return Упакованное 32-битное значение.
 *         Возвращает (u32)-1 в случае ошибки (если id1 или id2 выходят за пределы).
 *         ВНИМАНИЕ: Проверка на выход за пределы добавлена для безопасности,
 *         но generate_limited_id должен предотвращать это.
 */
static inline u32 pack_ids(int id1, int id2)
{
    // Проверка на допустимость значений (хотя generate_limited_id должен это гарантировать)
    if (!IS_ID_VALID(id1) || !IS_ID_VALID(id2))
    {
        // ERR("Invalid ID range: id1=%d, id2=%d\n", id1, id2);
        return -EINVAL; // Индикация ошибки
    }
    // Сдвигаем id1 влево на 16 бит и объединяем с id2 через побитовое ИЛИ
    return ((u32)id1 << 16) | ((u32)id2 & 0xFFFF);
}

/**
 * @brief Извлекает первый ID (старшие 16 бит) из упакованного значения.
 * @param packed_id Упакованное 32-битное значение.
 * @return Первый ID (как int).
 */
static inline int unpack_id1(u32 packed_id)
{
    // Сдвигаем вправо на 16 бит
    return (int)(packed_id >> 16);
}

/**
 * @brief Извлекает второй ID (младшие 16 бит) из упакованного значения.
 * @param packed_id Упакованное 32-битное значение.
 * @return Второй ID (как int).
 */
static inline int unpack_id2(u32 packed_id)
{
    // Применяем маску, чтобы оставить только младшие 16 бит
    return (int)(packed_id & 0xFFFF);
}

// Запаковка server_id + shm_id или client_id + shm_id
#define PACK_SC_SHM(ser_cli_id, shm_id) pack_ids(ser_cli_id, shm_id);

// Распаковка server_id + shm_id или client_id + shm_id
#define UNPACK_SC_SHM(packed_id, ser_cli_id, shm_id) \
    ser_cli_id = unpack_id1(packed_id);              \
    shm_id = unpack_id2(packed_id);

#endif // !ID_PACK_H