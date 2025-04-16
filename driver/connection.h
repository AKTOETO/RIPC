#ifndef CONNECTION_H
#define CONNECTION_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

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
    struct sub_mem_t *m_mem_p;
    atomic_t m_serv_mmaped; // отображена ли общая память на сервер
    struct list_head list;
};

// Список соединений и его блокировка
extern struct list_head g_conns_list;
extern struct mutex g_conns_lock;

/**
 * Операции над объектом соединения
 */

// создание соединения
struct connection_t *create_connection(
    struct client_t *client,
    struct server_t *server,
    struct sub_mem_t *mem);

// поиск соединения между двумя процессами
struct connection_t *find_connection(
    struct task_struct *task1,
    struct task_struct *task2);

// удаление соединения
void delete_connection(struct connection_t *con);

/**
 * Операции над глобальным списком серверов
 */

// удаление списка
void delete_connection_list(void);

#endif // !CONNECTION_H