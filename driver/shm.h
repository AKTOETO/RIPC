#ifndef SHM_H
#define SHM_H

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "id.h"
#include "ripc.h"

/**
 * Описание структуры общей памяти и функций работы с ней
 */

// Область общих памятей
struct shm_t
{
    int m_id; // идентификатор области памяти
    short m_num_of_pages; // размер общей памяти
    struct page *m_pages_p; // страницы памяти, выделенные под этот memory pool
    size_t m_size; // размер пула в байтах
    
    // Массив подобластей памяти
    struct sub_mem_t
    {
        int m_id;
        struct shm_t *m_shm;           // родительская область памяти
        struct page *m_pages_p;        // страницы памяти, относящиеся к этой области
        size_t m_size;                 // размер памяти в байтах
        struct connection_t *m_conn_p; // соединение между клиентом и сервером
    } m_sub_mems[SHM_POOL_SIZE];

    struct list_head list; // список областей памяти
};

// Список соединений и его блокировка
extern struct list_head g_shm_list;
extern struct mutex g_shm_lock;

/**
 * Операции над областью общих памятей
 */

// создание области общей памяти
struct shm_t *shm_create(void);

// удаление области общей памяти
void shm_destroy(struct shm_t *shm);

// получить свободную область
struct sub_mem_t *shm_get_free_submem(struct shm_t *shm);

/**
 * Операции над подобластью
 */

// подключение подобласти
void submem_add_connection(struct sub_mem_t* sub, struct connection_t* con);

// инициализация подобласти
struct sub_mem_t *submem_init(int id, struct shm_t *shm);

// очистка подобласти
void submem_clear(struct sub_mem_t *);

// отсоединить область
void submem_return(struct sub_mem_t *sub);

/**
 * Операции над глобальным списком
 */

// удаление списка
void delete_shm_list(void);

// получение свободной подобласти памяти
struct sub_mem_t *get_free_submem(void);

#endif // !SHM_H