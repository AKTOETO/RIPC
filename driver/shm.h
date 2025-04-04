#ifndef SHM_H
#define SHM_H

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "id.h"

/**
 * Описание структуры общей памяти и функций работы с ней
 */

// Общая память
struct shm_t
{
    int m_id;               // идентификатор области памяти
    void *m_mem_p;          // указатеь на область общей памяти
    size_t m_size;          // размер общей памяти
    atomic_t m_is_writing;  // флаг: пишет ли кто-то в память или нет
    atomic_t m_num_of_conn; // количество подключенных клиентов
    struct list_head list;  // список областей памяти
};

// Список соединений и его блокировка
extern struct list_head g_shm_list;
extern struct mutex g_shm_lock;

/**
 * Операции над объектом соединения
 */

// создание области общей памяти
struct shm_t *shm_create(size_t size);

// удаление области общей памяти
void shm_destroy(struct shm_t *shm);

// начать запись в память
int shm_start_write(struct shm_t *shm);

// закончить запись в память
void shm_end_write(struct shm_t *shm);

/**
 * Операции над глобальным списком
 */

// удаление списка
void delete_shm_list(void);

#endif // !SHM_H