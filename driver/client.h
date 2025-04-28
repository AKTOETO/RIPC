#ifndef CLIENT_H
#define CLIENT_H
#include "id.h"
#include "connection.h"

#include <linux/list.h>
#include <linux/sched.h>

/**
 * Определение структуры клиента и операций над ней
 */

struct client_t
{
    int m_id;                        // id клиента в процессе
    struct clients_list_t *m_task_p; // указатель на задачу, где зарегистрирован сервер
    struct connection_t *m_conn_p;   // указатель на соединение с сервером и пмаятью
    struct list_head list;           // список клиентов
};

// Список соединений и его блокировка
extern struct list_head g_clients_list;
extern struct mutex g_clients_lock;

/**
 * Операции над объектом соединения
 */
// создание клиента
struct client_t *client_create(void);

// прикрепление к определенному процессу
void client_add_task(struct client_t *cli, struct clients_list_t *task);

// очистка соединения клиента
void client_cleanup_connection(struct client_t *cli);

// удаление клиента
void client_destroy(struct client_t *cli);

// подключение клиента к соединению
void client_add_connection(struct client_t *cli, struct connection_t *con);

// поиск клиента по id
struct client_t *find_client_by_id(int id);

// поиск клиента по id и pid
struct client_t *find_client_by_id_pid(int id, pid_t pid);

/**
 * Операции над глобальным списком клиентов
 */

// удаление списка
void delete_client_list(void);

#endif // !CLIENT_H