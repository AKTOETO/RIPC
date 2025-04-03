#include "client.h"
#include "err.h"

#include <linux/mm.h>

// Список соединений и его блокировка
LIST_HEAD(g_clients_list);
DEFINE_MUTEX(g_clients_lock);

// генератор id для списка клиентов
//DEFINE_ID_GENERATOR(g_client_id_gen);


// создание клиента
struct client_t *client_create(void)
{
    struct client_t *cli = kmalloc(sizeof(*cli), GFP_KERNEL);
    if (!cli)
    {
        ERR("Cant allocate memory for client");
        return NULL;
    }

    // Инициализация полей
    cli->m_id = generate_id(&g_id_gen);
    cli->m_conn_p = NULL;
    cli->m_task_p = current;

    mutex_lock(&g_clients_lock);
    list_add_tail(&cli->list, &g_clients_list);
    mutex_unlock(&g_clients_lock);

    INF("Client %d created", cli->m_id);

    return cli;
}

// удаление клиента
void client_destroy(struct client_t *cli)
{
    // проверка входных данных
    if (!cli)
    {
        ERR("Attempt to destroy NULL client");
        return;
    }

    INF("Destroying client %d\n", cli->m_id);

    // удаление из глобального списка
    mutex_lock(&g_clients_lock);
    list_del(&cli->list);
    mutex_unlock(&g_clients_lock);

    free_id(&g_id_gen, cli->m_id);
    kfree(cli);
}

// поиск клиента по id
struct client_t *find_client_by_id(int id)
{
    // проверка входных данных
    if (id < 0)
    {
        ERR("find_client_by_id: Incorrect id: %d", id);
        return NULL;
    }

    mutex_lock(&g_clients_lock);
    
    // проходимся по каждому клиенту и ищем подходящего
    struct client_t *client = NULL;
    
    // Итерируемся по списку клиентов
    list_for_each_entry(client, &g_clients_list, list)
    {
        if (client->m_id == id)
        {
            mutex_unlock(&g_clients_lock);
            // Нашли совпадение - сохраняем результат
            return client;
        }
    }
    mutex_unlock(&g_clients_lock);

    return NULL;
}


struct client_t *find_client_by_id_pid(int id, pid_t pid)
{
    // проверка входных данных
    if (id < 0)
    {
        ERR("find_client_by_id: Incorrect id: %d", id);
        return NULL;
    }

    mutex_lock(&g_clients_lock);
    
    // проходимся по каждому клиенту и ищем подходящего
    struct client_t *client = NULL;
    
    // Итерируемся по списку клиентов
    list_for_each_entry(client, &g_clients_list, list)
    {
        if (client->m_task_p->pid == pid && client->m_id == id)
        {
            mutex_unlock(&g_clients_lock);
            // Нашли совпадение - сохраняем результат
            return client;
        }
    }
    mutex_unlock(&g_clients_lock);

    return NULL;
}

/**
 * Операции над глобальным списком клиентов
 */

// удаление списка
void delete_client_list()
{
    struct client_t *cl, *temp;
    list_for_each_entry_safe(cl, temp, &g_clients_list, list)
        client_destroy(cl);
    
    // удаление генератора id
    //DELETE_ID_GENERATOR(&g_client_id_gen);
}