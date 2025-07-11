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
    int m_id;                    // id клиента в процессе
    struct servers_list_t* m_task_p; // указатель на задачу, где зарегистрирован сервер
    struct serv_conn_list_t
    {
        struct connection_t *conn; // указатель на соединение
        struct list_head list;
    } connection_list;            // список установленных подключений
    struct mutex m_con_list_lock; // блокировка списка соединений
    struct mutex m_lock;          // блокировка доступа к серверу
    struct list_head list;        // список серверов
};

// Список серверов и его блокировка
extern struct list_head g_servers_list;
extern struct mutex g_servers_lock;

/**
 * Операции над сервером
 */

// создание сервера
struct server_t *server_create(const char *name);

// прикрепление к определенному процессу
void server_add_task(struct server_t *srv, struct servers_list_t*task);

// очистка одного соединения сервера
void server_cleanup_connection(
    struct server_t* srv, struct serv_conn_list_t* scon);

// очистка всех соединений сервера
void server_cleanup_connections(struct server_t *srv);

// удаление сервера
void server_destroy(struct server_t *srv);

// поиск сервера по имени
struct server_t *find_server_by_name(const char *name);

// поиск сервера по id и pid
struct server_t *find_server_by_id_pid(int id, pid_t pid);

// поиск сервера по id и pid
struct server_t *find_server_by_id(int id);

// поиск клиента из списка сервера по task_struct
struct client_t *find_client_by_task_from_server(
    struct task_struct *task, struct server_t *serv);

// добавление клиента к серверу
int connect_client_to_server(struct server_t *srv, struct client_t *cli);

// добавление соединения
void server_add_connection(struct server_t *srv, struct connection_t *con);

// удаление соединения
void server_delete_connection(struct server_t *srv, struct serv_conn_list_t *con);

// поиск соединения с необходимой памятью
struct serv_conn_list_t *server_find_conn_by_sub_mem_id(
    struct server_t *srv, int sub_mem_id);

// поиск соединения по глобальной структуре соединения
struct serv_conn_list_t *server_find_conn(
    struct server_t *srv, struct connection_t *con);

// получение информации о сервере
void server_get_data(struct server_t* srv, struct st_server* dest);

/**
 * Операции над глобальным списком серверов
 */

// удаление списка
void delete_server_list(void);

#endif // !SERVER_H