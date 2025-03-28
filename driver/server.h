#ifndef SERVER_H
#define SERVER_H
#include "id.h"
#include "ripc.h"
#include "connection.h"
#include <linux/list.h>

/**
 * Определение структуры сервера и операций над ней
 */

struct server_t
{
    char m_name[MAX_SERVER_NAME];
    int m_id;                         // id клиента в процессе
    struct task_struct *m_task_p;     // указатель на задачу, где зарегистрирован сервер
    struct list_head connection_list; // список установленных подключений
    struct list_head list;            // список серверов
};

// Список соединений и его блокировка
extern struct list_head g_servers_list;
extern struct mutex g_servers_lock;

// генератор id для списка серверов
extern struct ida g_servers_id_gen;

/**
 * Операции над объектом соединения
 */

// создание сервера
struct server_t *server_create(const char *name);

// удаление сервера
void server_destroy(struct server_t *srv);

// поиск сервера по имени
struct server_t *find_server_by_name(const char *name);

// поиск клиента из списка сервера по task_struct
struct client_t *find_client_by_task_from_server(
    struct task_struct *task, struct server_t *serv);

// добавление клиента к серверу
int connect_client_to_server(struct server_t *srv, struct client_t *cli);

// добавление соединения
void server_add_connection(struct server_t *srv, struct connection_t *con);

// удаление соединения
void server_delete_connection(struct server_t *srv, struct connection_t *con);

/**
 * Операции над глобальным списком серверов
 */

// удаление списка
void delete_server_list(void);

#endif // !SERVER_H