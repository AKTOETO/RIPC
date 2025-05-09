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
    list_add_tail(&con->list, &g_conns_list);
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

// отсоединение sub_mem от соединения
static void safe_disconnect_submem(struct connection_t *conn)
{
    if (conn && conn->m_mem_p)
    {
        struct sub_mem_t *sub = conn->m_mem_p;
        INF("Disconnecting sub_mem %d from conn %p", sub->m_id, conn);
        if (sub->m_conn_p == conn)
        {
            sub->m_conn_p = NULL; // Помечаем sub_mem как свободную
        }
        else
        {
            INF("sub_mem %d connection pointer mismatch!", sub->m_id);
        }
        conn->m_mem_p = NULL; // Убираем ссылку из соединения
    }
}

// удаление соединения
void delete_connection(struct connection_t *conn)
{
    // проверка входных данных
    if (!conn)
    {
        ERR("Attempt to destroy NULL connection");
        return;
    }

    int ret = 0;

    INF("Deleting connection: ClientID=%d, ServerID=%d, SubMemID=%d",
        conn->m_client_p ? conn->m_client_p->m_id : -1,
        conn->m_server_p ? conn->m_server_p->m_id : -1,
        conn->m_mem_p ? conn->m_mem_p->m_id : -1);

    // Отсоединяем sub_mem
    safe_disconnect_submem(conn);

    // Уведомляем другого участника (если он есть), что соединение разорвано
    // Это важно, чтобы у другого участника не остался висячий указатель conn->m_client_p/m_server_p
    // и чтобы соединение было удалено из списка сервера.

    // отсоединение от клиента
    if (conn->m_client_p)
    {
        // Сервер ушел, уведомляем клиента
        if (conn->m_client_p->m_conn_p == conn)
        {
            conn->m_client_p->m_conn_p = NULL;
            INF("Cleared connection pointer in client %d", conn->m_client_p->m_id);

            // отправляем уведомление клиенту 
            if((ret = notification_send(SERVER, REMOTE_DISCONNECT, conn)) != 0)
            {
                ERR("Bad notification sending. code: %d", ret);
            }
        }
        else
        {
            // Этого не должно быть, если указатели синхронны
            INF("Connection points to client %d, but client points elsewhere!", conn->m_client_p->m_id);
        }
        conn->m_client_p = NULL; // Обнуляем в соединении
    }

    // отсоединение от сервера
    if (conn->m_server_p)
    {
        // Клиент ушел, уведомляем сервер и удаляем из его списка
        struct serv_conn_list_t *srv_conn_entry;
        INF("Removing connection from server %d's list", conn->m_server_p->m_id);
        srv_conn_entry = server_find_conn(conn->m_server_p, conn); // Находим запись в списке сервера

        mutex_lock(&conn->m_server_p->m_con_list_lock);
        if (srv_conn_entry)
        {
            // отправляем уведомление Серверу 
            if((ret = notification_send(CLIENT, REMOTE_DISCONNECT, conn)) != 0)
            {
                ERR("Bad notification sending. code: %d", ret);
            }

            list_del(&srv_conn_entry->list); // Удаляем из списка сервера
            kfree(srv_conn_entry);           // Освобождаем элемент списка
            INF("Connection removed from server %d list.", conn->m_server_p->m_id);
        }
        else
        {
            INF("Connection not found in server %d's list!", conn->m_server_p->m_id);
        }
        mutex_unlock(&conn->m_server_p->m_con_list_lock);
        conn->m_server_p = NULL; // Обнуляем в соединении
    }

    // удаление из общего списка
    mutex_lock(&g_conns_lock);
    list_del(&conn->list);
    INF("deleted conn from list");
    mutex_unlock(&g_conns_lock);

    // очистка памяти
    kfree(conn);

    conn = NULL;
    INF("Connection structure freed.");
}

void delete_connection_list(void)
{
    // для безопасного удаления
    struct connection_t *con, *tmp;

    // Удаление списка соединений
    list_for_each_entry_safe(con, tmp, &g_conns_list, list)
        delete_connection(con);
}
