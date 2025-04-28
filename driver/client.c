#include "client.h"
#include "err.h"
#include "connection.h"
#include "task.h"

#include <linux/mm.h>

// Список соединений и его блокировка
LIST_HEAD(g_clients_list);
DEFINE_MUTEX(g_clients_lock);

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
    cli->m_task_p = NULL;

    mutex_lock(&g_clients_lock);
    list_add_tail(&cli->list, &g_clients_list);
    mutex_unlock(&g_clients_lock);

    INF("Client %d created", cli->m_id);

    return cli;
}

void client_add_task(struct client_t *cli, struct clients_list_t *task)
{
    if (!cli || !task)
    {
        ERR("empty param");
        return;
    }

    // если уже клиент подключен к кому-то,
    // то нельзя его переподключить
    if (cli->m_task_p)
    {
        ERR("Client already connected to task (PID:%d)",
            cli->m_task_p->m_reg_task->m_task_p->pid);
        return;
    }

    // подключаем сервер
    cli->m_task_p = task;
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

    INF("Destroying client (ID:%d)(PID:%d)\n",
        cli->m_id, cli->m_task_p->m_reg_task->m_task_p->pid);

    // удаление соединения, если оно есть
    if (cli->m_conn_p)
    {
        cli->m_conn_p->m_client_p = NULL;
        delete_connection(cli->m_conn_p);
    }

    // удаление из глобального списка
    mutex_lock(&g_clients_lock);
    list_del(&cli->list);
    mutex_unlock(&g_clients_lock);

    free_id(&g_id_gen, cli->m_id);
    kfree(cli);
}

void client_add_connection(
    struct client_t *cli,
    struct connection_t *con)
{
    if (!cli || !con)
    {
        ERR("There is no cli or con");
        return;
    }

    if (cli->m_conn_p)
    {
        ERR("There is connection in client");
        return;
    }

    cli->m_conn_p = con;
}

// поиск клиента по id
struct client_t *find_client_by_id(int id)
{
    // проверка входных данных
    if (!IS_ID_VALID(id))
    {
        ERR("Incorrect id: %d", id);
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
    if (!IS_ID_VALID(id))
    {
        ERR("Incorrect id: %d", id);
        return NULL;
    }

    mutex_lock(&g_clients_lock);

    // проходимся по каждому клиенту и ищем подходящего
    struct client_t *client = NULL;

    // Итерируемся по списку клиентов
    list_for_each_entry(client, &g_clients_list, list)
    {
        if (client->m_task_p->m_reg_task->m_task_p->pid == pid &&
            client->m_id == id)
        {
            INF("FOUND client (ID:%d)(PID:%d)",
                client->m_id, client->m_task_p->m_reg_task->m_task_p->pid);
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
}