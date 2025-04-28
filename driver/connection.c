#include "connection.h"
#include "err.h"

#include <linux/mm.h>
#include "task.h"

// Список соединений и его блокировка
LIST_HEAD(g_conns_list);
DEFINE_MUTEX(g_conns_lock);

// создание соединения
struct connection_t *create_connection(
    struct client_t *client,
    struct server_t *server,
    struct sub_mem_t *mem)
{
    if (!client || !server || !mem)
    {
        ERR("Client or server or mem pointer is NULL");
        return NULL;
    }

    struct connection_t *con = kmalloc(sizeof(*con), GFP_KERNEL);

    if (!con)
    {
        ERR("cant create connection object");
        return NULL;
    }

    // инициализация полей
    con->m_client_p = client;
    con->m_mem_p = mem;
    con->m_server_p = server;
    INIT_LIST_HEAD(&con->list);
    atomic_set(&con->m_serv_mmaped, 0);

    // добавление в глобальный список
    mutex_lock(&g_conns_lock);
    list_add(&con->list, &g_conns_list);
    mutex_unlock(&g_conns_lock);

    INF("Created new connection btw serv: %d client: %d shm: %d",
        client->m_id, server->m_id, mem->m_id);

    return con;
}

// поиск соединения между двумя процессами
struct connection_t *find_connection(
    struct task_struct *task1,
    struct task_struct *task2)
{
    if (!task1 || !task2)
    {
        ERR("Empty task structs");
        return NULL;
    }

    INF("Finding connection btw (PID:%d) and (PID:%d)",
        task1->pid, task2->pid);

    // проходимся по списку и ищем общую память между двумя процессами
    struct connection_t *con = NULL;
    mutex_lock(&g_conns_lock);
    list_for_each_entry(con, &g_conns_list, list)
    {
        if ((con->m_client_p->m_task_p->m_reg_task->m_task_p == task1 &&
             con->m_server_p->m_task_p->m_reg_task->m_task_p == task2) ||
            (con->m_client_p->m_task_p->m_reg_task->m_task_p == task2 &&
             con->m_server_p->m_task_p->m_reg_task->m_task_p == task1))
        {
            INF("FOUND connection");
            mutex_unlock(&g_conns_lock);
            return con;
        }
    }
    INF("Connection not found");
    mutex_unlock(&g_conns_lock);

    return NULL;
}

// удаление соединения
void delete_connection(struct connection_t *con)
{
    // проверка входных данных
    if (!con)
    {
        ERR("Attempt to destroy NULL connection");
        return;
    }

    INF("deleting connection");

    // Удаление сервера
    if (con->m_server_p)
    {
        struct serv_conn_list_t *cn = server_find_conn(con->m_server_p, con);
        if (!cn)
        {
            ERR("Server's connection has not connection ptr");
        }
        else
        {
            cn->conn = NULL;
            server_destroy(con->m_server_p);
        }
    }

    // Удаление клиента
    if (con->m_client_p)
    {
        con->m_client_p->m_conn_p = NULL;
        client_destroy(con->m_client_p);
    }

    // Удаление памяти
    if (con->m_mem_p)
        submem_disconnect(con->m_mem_p, con);

    // удаление из общего списка
    mutex_lock(&g_conns_lock);
    list_del(&con->list);
    mutex_unlock(&g_conns_lock);

    // очистка памяти
    kfree(con);
}

void delete_connection_list(void)
{
    // для безопасного удаления
    struct connection_t *con, *tmp;

    // Удаление списка соединений
    list_for_each_entry_safe(con, tmp, &g_conns_list, list)
        delete_connection(con);
}
