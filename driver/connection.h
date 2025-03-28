#ifndef CONNECTION_H
#define CONNECTION_H

#include <linux/list.h>
#include <linux/mutex.h>

#include "client.h"
#include "server.h"
#include "shm.h"

/**
 * Структура, описывающая соединение клиента и сервера
 */

struct connection_t
{
    struct client_t *m_client_p;
    struct server_t *m_server_p;
    struct shm_t *m_mem_p;
    struct list_head list;
};

/**
 * Операции над объектом соединения
 */

// создание соединения
struct connection_t *create_connection(
    struct client_t *client,
    struct server_t *server,
    struct shm_t *mem);

// удаление соединения
void delete_connection(struct connection_t *con);

#endif // !CONNECTION_H