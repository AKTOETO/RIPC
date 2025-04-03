#include "connection.h"
#include "err.h"

#include <linux/mm.h>

// создание соединения
struct connection_t *create_connection(
    struct client_t *client,
    struct server_t *server,
    struct shm_t *mem)
{
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

    return con;
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

    // очистка памяти
    kfree(con);
}