#include "shm.h"
#include "err.h"

#include <linux/mm.h>

// Список соединений и его блокировка
LIST_HEAD(g_shm_list);
DEFINE_MUTEX(g_shm_lock);

/**
 * Операции над объектом соединения
 */

// создание области общей памяти
struct shm_t *shm_create()
{
    INF("Create sheared memory pool");

    // выделяем память под структуру
    struct shm_t *shm = kmalloc(sizeof(*shm), GFP_KERNEL);
    if (!shm)
    {
        ERR("Cant allocate shared memory struct");
        return NULL;
    }

    // аллоцируем страницы памяти
    shm->m_num_of_pages = SHM_POOL_PAGE_NUMBER;
    shm->m_pages_p = alloc_pages(GFP_KERNEL | __GFP_ZERO, SHM_REGION_ORDER);

    if (!shm->m_pages_p)
    {
        ERR("Cant allocate %d pages", shm->m_num_of_pages);
        goto failed_page_alloc;
    }

    // помечаем страницы невытесняемыми, чтобы ядро их не вытеснило
    SetPageReserved(shm->m_pages_p);

    // инициализация под областей
    for (int i = 0; i < SHM_POOL_SIZE; i++)
    {
        submem_init(i, shm);
    }

    // генерация id
    shm->m_id = generate_id(&g_id_gen);

    shm->m_size = SHM_POOL_BYTE_SIZE;

    // добавление в список общих паметей
    INIT_LIST_HEAD(&shm->list);
    mutex_lock(&g_shm_lock);
    list_add(&shm->list, &g_shm_list);
    mutex_unlock(&g_shm_lock);

    INF("Shared memory allocated (ID:%d)", shm->m_id);

    return shm;

failed_page_alloc:
    __free_pages(shm->m_pages_p, SHM_REGION_ORDER);

    kfree(shm);
    return NULL;
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

    // очистка подобластей
    for (int i = 0; i < SHM_POOL_SIZE; i++)
        submem_clear(&shm->m_sub_mems[i]);

    // очистка страниц памяти
    ClearPageReserved(shm->m_pages_p);
    __free_pages(shm->m_pages_p, SHM_REGION_ORDER);

    // удаление памяти из общего списка
    mutex_lock(&g_shm_lock);
    list_del(&shm->list);
    mutex_unlock(&g_shm_lock);

    // освобождение памяти под структуру
    kfree(shm);
}

struct sub_mem_t *shm_get_free_submem(struct shm_t *shm)
{
    if (!shm)
    {
        ERR("There is not shm");
        return NULL;
    }

    // поиск свободной области
    for (int i = 0; i < SHM_POOL_SIZE; i++)
    {
        if (!shm->m_sub_mems[i].m_conn_p)
        {
            INF("FOUND free sub mem (ID: %d)", shm->m_sub_mems[i].m_id);
            return &shm->m_sub_mems[i];
        }
    }
    INF("free sub mem not found");

    return NULL;
}
void submem_add_connection(
    struct sub_mem_t *sub,
    struct connection_t *con)
{
    if(!sub || !con)
    {
        ERR("There is no sub or con");
        return;
    }

    sub->m_conn_p = con;
}

struct sub_mem_t *submem_init(int id, struct shm_t *shm)
{
    INF("Init submem");

    if (!shm)
    {
        ERR("There is not shm");
        return NULL;
    }

    if (!IS_ID_VALID(id) || id > SHM_POOL_SIZE)
    {
        ERR("Incorrect (ID: %d) (POOL SIZE:%d)", id, SHM_POOL_SIZE);
        return NULL;
    }

    struct sub_mem_t *sub = &shm->m_sub_mems[id];

    // установка родительской области памяти
    sub->m_shm = shm;

    // получение id
    sub->m_id = generate_id(&g_id_gen);

    // количество байт на подобласть
    sub->m_size = SHM_REGION_PAGE_SIZE;

    // нет текущего подключения
    sub->m_conn_p = NULL;

    // получение страниц памяти для этой подпамяти
    sub->m_pages_p = shm->m_pages_p + id * SHM_REGION_PAGE_NUMBER;

    return sub;
}

void submem_clear(struct sub_mem_t *sub)
{
    if (!sub)
    {
        ERR("There is not submem");
        return;
    }
    INF("Deleting sub mem (ID: %d)(BYTE SIZE: %ld)", sub->m_id, sub->m_size);

    // удаление id
    free_id(&g_id_gen, sub->m_id);
}

int submem_disconnect(struct sub_mem_t *sub, struct connection_t* con)
{
    if (!sub)
    {
        ERR("There is not submem");
        return -1;
    }
    if(!con)
    {
        ERR("There is not con");
        return -1;
    }
    if(!sub->m_conn_p)
    {
        ERR("There is not sub->con");
        return -1;
    }

    if(con != sub->m_conn_p)
    {
        ERR("not the same connection was passed");
        return -1;
    }

    INF("submem disconnect");

    // отключение от соединения
    sub->m_conn_p = NULL;

    return 0;
}

/**
 * Глобальный список
 */

// удаление списка
void delete_shm_list(void)
{
    struct shm_t *shm, *tmp;
    list_for_each_entry_safe(shm, tmp, &g_shm_list, list)
        shm_destroy(shm);
}

struct sub_mem_t *get_free_submem(void)
{
    INF("Getting free submem");
    struct shm_t *shm = NULL;
    struct sub_mem_t *sub = NULL;

    // проходимся по всему списку и ищем свободную память
    list_for_each_entry(shm, &g_shm_list, list)
    {
        sub = shm_get_free_submem(shm);
        if (sub)
        {
            INF("FOUND free submem (ID: %d)", sub->m_id);
            return sub;
        }
    }

    // если нет свободных подобластей, создаем новую область с подобластями
    INF("There is no free submem");
    shm = shm_create();

    // получаем свободную подобласть из нее и возвращаем
    sub = shm_get_free_submem(shm);
    return sub;
}
