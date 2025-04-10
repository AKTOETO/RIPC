#include "shm.h"
#include "ripc.h"
#include "err.h"

#include <linux/mm.h>

// Список соединений и его блокировка
LIST_HEAD(g_shm_list);
DEFINE_MUTEX(g_shm_lock);

/**
 * Операции над объектом соединения
 */

// создание области общей памяти
struct shm_t *shm_create(size_t size)
{
    // проверка входных параметров
    if (size == 0 || size > MAX_SHARED_MEM_SIZE)
    {
        ERR("Invalid shared memory size: %ld", size);
        return NULL;
    }

    // выделяем память под структуру
    struct shm_t *shm = kmalloc(sizeof(*shm), GFP_KERNEL);
    if (!shm)
    {
        ERR("Cant allocate shared memory struct");
        return NULL;
    }

    // выделякм память под саму область
    shm->m_mem_p = kmalloc(size, GFP_KERNEL);
    if (!shm->m_mem_p)
    {
        ERR("Cant allocate shared memory area");
        kfree(shm);
        return NULL;
    }

    // инициализация полей
    shm->m_id = generate_id(&g_id_gen);
    shm->m_size = size;
    atomic_set(&shm->m_is_writing, 0);
    atomic_set(&shm->m_num_of_conn, 0);
    //shm->m_conn_p = NULL;
    INIT_LIST_HEAD(&shm->list);

    // добавление в список общих паметей
    mutex_lock(&g_shm_lock);
    list_add(&shm->list, &g_shm_list);
    mutex_unlock(&g_shm_lock);

    INF("Shared memory allocated (ID:%d) (%zu bytes)", shm->m_id, shm->m_size);

    return shm;
}

// удаление области общей памяти
void shm_destroy(struct shm_t *shm)
{
    // проверка входных параметров
    if (!shm)
    {
        ERR("Attempt to destroy NULL shared memory");
        return;
    }

    INF("Destroying shared memory (ID:%d)(%zu bytes)", shm->m_id, shm->m_size);

    // освобождение id
    free_id(&g_id_gen, shm->m_id);

    mutex_lock(&g_shm_lock);

    // освобождение память под общую область
    if (shm->m_mem_p)
        kfree(shm->m_mem_p);

    // удаление памяти из общего списка
    list_del(&shm->list);

    // освобождение памяти под структуру
    kfree(shm);

    mutex_unlock(&g_shm_lock);
}

// начать запись в память
int shm_start_write(struct shm_t *shm)
{
    // проверка входных данных
    if (!shm)
    {
        ERR("shm_start_write: Attempt to write to NULL shared memory");
        return -1;
    }

    // Пытаемся изменить 0 → 1 атомарно
    if (atomic_cmpxchg(&shm->m_is_writing, 0, 1) == 0)
    {
        return 0; // Успешно захватили флаг
    }
    return -EBUSY; // Кто-то уже пишет
}

// закончить запись в память
void shm_end_write(struct shm_t *shm)
{
    // проверка входных данных
    if (!shm)
    {
        ERR("shm_end_write: Attempt to end writing to NULL shared memory");
        return;
    }

    // Гарантированно сбрасываем флаг
    atomic_set(&shm->m_is_writing, 0);
}

// удаление списка
void delete_shm_list(void)
{
    struct shm_t *shm, *tmp;
    list_for_each_entry_safe(shm, tmp, &g_shm_list, list)
        shm_destroy(shm);
}