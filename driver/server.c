#include "server.h"
#include "client.h"
#include "err.h"
#include "ripc.h"
#include "shm.h"
#include "task.h"

#include <linux/mm.h>     // операции с памятью
#include <linux/sched.h>  // для current
#include <linux/string.h> // операции над строками

// Список соединений и его блокировка
LIST_HEAD(g_servers_list);
DEFINE_MUTEX(g_servers_lock);

/**
 * Операции над объектом соединения
 */

// создание сервера
struct server_t *server_create(const char *name)
{
    // проверка входных данные
    if (!name || strlen(name) == 0)
    {
        ERR("Invalid server name");
        return NULL;
    }

    // выделение и проверка памяти
    struct server_t *srv = kmalloc(sizeof(*srv), GFP_KERNEL);
    if (!srv)
    {
        ERR("server_create: Cant allocate memory for server");
        return NULL;
    }

    // Инициализация полей
    strscpy(srv->m_name, name, MAX_SERVER_NAME);
    srv->m_id = generate_id(&g_id_gen);
    INIT_LIST_HEAD(&srv->connection_list.list);
    srv->m_task_p = NULL;

    // инициализация блокировок
    mutex_init(&srv->m_lock);
    mutex_init(&srv->m_con_list_lock);

    // добавление в главный список
    mutex_lock(&g_servers_lock);
    list_add_tail(&srv->list, &g_servers_list);
    mutex_unlock(&g_servers_lock);

    INF("Server '%s' (ID: %d) created", srv->m_name, srv->m_id);

    return srv;
}

void server_add_task(struct server_t *srv, struct servers_list_t *task)
{
    if (!srv || !task)
    {
        ERR("empty param");
        return;
    }

    mutex_lock(&srv->m_lock);

    // если уже сервер подключен к кому-то,
    // то нельзя его переподключить
    if (srv->m_task_p)
    {
        ERR("Server already connected to task (PID:%d)", srv->m_task_p->m_reg_task->m_task_p->pid);
        mutex_unlock(&srv->m_lock);
        return;
    }

    // подключаем сервер
    srv->m_task_p = task;
    mutex_unlock(&srv->m_lock);
}

void server_cleanup_connection(struct server_t *srv, struct serv_conn_list_t *scon)
{
    if (!srv || !scon)
    {
        ERR("Invalid params");
        return;
    }

    // проверка на принадлежность соединения серверу
    if (scon->conn->m_server_p != srv)
    {
        ERR("ABORT: Server is not the owner of this connection");
        return;
    }

    // удаление соединения
    mutex_lock(&srv->m_lock);
    mutex_lock(&srv->m_con_list_lock);

    struct connection_t *conn = scon->conn;
    list_del(&scon->list);
    kfree(scon);

    if (conn)
    {
        INF("Processing connection for client %d, sub_mem %d", conn->m_client_p ? conn->m_client_p->m_id : -1,
            conn->m_mem_p ? conn->m_mem_p->m_id : -1);
        // Вызываем delete_connection, которая обработает и другую сторону
        // и удалит соединение из глобального списка.
        // Разблокируем мьютекс сервера перед вызовом, т.к. delete_connection
        // может блокировать глобальный список соединений.
        mutex_unlock(&srv->m_con_list_lock);
        mutex_unlock(&srv->m_lock);

        delete_connection(conn); // Удалит соединение, отсоединит sub_mem и клиента

        mutex_lock(&srv->m_lock);
        mutex_lock(&srv->m_con_list_lock); // Снова блокируем для след. итерации
    }
    else
    {
        INF("Found NULL connection pointer in server's connection list");
    }

    mutex_unlock(&srv->m_con_list_lock);
    mutex_unlock(&srv->m_lock);
}

void server_cleanup_connections(struct server_t *srv)
{
    struct serv_conn_list_t *srv_conn_entry, *tmp;

    if (!srv)
        return;
    INF("Cleaning up connections for server %d ('%s')", srv->m_id, srv->m_name);
    list_for_each_entry_safe(srv_conn_entry, tmp, &srv->connection_list.list, list)
    {
        server_cleanup_connection(srv, srv_conn_entry);
    }
    INF("Finished cleaning connections for server %d", srv->m_id);
}

// удаление сервера
void server_destroy(struct server_t *srv)
{
    // проверка входного параметра
    if (!srv)
    {
        ERR("server_destroy: Attempt to destroy NULL server");
        return;
    }

    INF("Destroying server (ID:%d)(NAME:%s)", srv->m_id, srv->m_name);

    // удаление сервера из глобального списка
    mutex_lock(&g_servers_lock);
    mutex_lock(&srv->m_lock);
    list_del(&srv->list);
    mutex_unlock(&srv->m_lock);
    mutex_unlock(&g_servers_lock);

    free_id(&g_id_gen, srv->m_id);
    mutex_destroy(&srv->m_lock);
    mutex_destroy(&srv->m_con_list_lock);
    kfree(srv);
}

// поиск сервера по имени
struct server_t *find_server_by_name(const char *name)
{
    mutex_lock(&g_servers_lock);
    struct server_t *srv = NULL;
    list_for_each_entry(srv, &g_servers_list, list)
    {
        if(!srv)
        {
            ERR("NULL server entry");
            mutex_unlock(&g_servers_lock);
            return NULL;
        }

        INF("Got a new server '%s'", srv->m_name);

        if (strcmp(srv->m_name, name) == 0)
        {
            mutex_unlock(&g_servers_lock);
            return srv;
        }
    }
    mutex_unlock(&g_servers_lock);
    return NULL;
}

// поиск сервера
struct server_t *find_server_by_id_pid(int id, pid_t pid)
{
    // проверка входных данных
    if (!IS_ID_VALID(id))
    {
        ERR("Incorrect id: %d", id);
        return NULL;
    }
    INF("Finding server with ID: %d PID: %d", id, pid);

    mutex_lock(&g_servers_lock);

    // проходимся по каждому клиенту и ищем подходящего
    struct server_t *server = NULL;

    // Итерируемся по списку клиентов
    list_for_each_entry(server, &g_servers_list, list)
    {
        if (!server)
        {
            ERR("NULL server entry");
            mutex_unlock(&g_servers_lock);
            return NULL;
        }

        mutex_lock(&server->m_lock);

        if (server->m_task_p && server->m_task_p->m_reg_task->m_task_p->pid == pid && server->m_id == id)
        {
            INF("FOUND server (ID:%d)(PID:%d)(NAME:%s)", server->m_id, server->m_task_p->m_reg_task->m_task_p->pid,
                server->m_name);

            mutex_unlock(&server->m_lock);
            mutex_unlock(&g_servers_lock);
            return server;
        }
        mutex_unlock(&server->m_lock);
    }
    INF("Server not found with (ID:%d)(PID:%d)", id, pid);
    mutex_unlock(&g_servers_lock);

    return NULL;
}

struct server_t *find_server_by_id(int id)
{
    // проверка входных данных
    if (!IS_ID_VALID(id))
    {
        ERR("Incorrect id: %d", id);
        return NULL;
    }
    INF("Finding server with ID: %d", id);

    mutex_lock(&g_servers_lock);

    // проходимся по каждому клиенту и ищем подходящего
    struct server_t *server = NULL;

    // Итерируемся по списку клиентов
    list_for_each_entry(server, &g_servers_list, list)
    {
        if (!server)
        {
            ERR("NULL server entry");
            mutex_unlock(&g_servers_lock);
            return NULL;
        }

        mutex_lock(&server->m_lock);

        if (server->m_id == id)
        {
            INF("FOUND server (ID:%d)(PID:%d)(NAME:%s)", server->m_id, server->m_task_p->m_reg_task->m_task_p->pid,
                server->m_name);

            mutex_unlock(&server->m_lock);
            mutex_unlock(&g_servers_lock);
            return server;
        }
        mutex_unlock(&server->m_lock);
    }
    mutex_unlock(&g_servers_lock);
    INF("Server not found with (ID:%d)", id);

    return NULL;
}

// поиск клиента из списка сервера по task_struct
struct client_t *find_client_by_task_from_server(struct task_struct *task, struct server_t *serv)
{
    // проверяем входные данные
    if (!serv || !task)
    {
        ERR("Invalid input params");
        return NULL;
    }

    // соединение с клиентом и памятью
    struct serv_conn_list_t *conn = NULL;

    mutex_lock(&serv->m_lock);
    mutex_lock(&serv->m_con_list_lock);
    // Итерируемся по списку подключений
    list_for_each_entry(conn, &serv->connection_list.list, list)
    {
        INF("\tcomapre connected PID: %d and requested PID: %d", task->pid,
            conn->conn->m_client_p->m_task_p->m_reg_task->m_task_p->pid);
        if (conn->conn->m_client_p->m_task_p->m_reg_task->m_task_p == task)
        {
            // Нашли совпадение - сохраняем результат
            mutex_unlock(&serv->m_con_list_lock);
            mutex_unlock(&serv->m_lock);
            return conn->conn->m_client_p;
        }
    }
    mutex_unlock(&serv->m_con_list_lock);
    mutex_unlock(&serv->m_lock);
    return NULL;
}

// добавление клиента к серверу
int connect_client_to_server(struct server_t *server, struct client_t *client)
{
    INF("Connecting client (ID:%d)(PID:%d) to server (ID:%d)(PID:%d)", client->m_id,
        client->m_task_p->m_reg_task->m_task_p->pid, server->m_id, server->m_task_p->m_reg_task->m_task_p->pid);

    // ищем свободную подобласть памяти
    struct sub_mem_t *sub = get_free_submem();

    // создаем объект соединения
    struct connection_t *con = create_connection(client, server, sub);

    // проверка создания объекта соединения
    if (!con)
    {
        ERR("CONNECT_TO_SERVER: failed to create connection_t object");
        goto falied_create_con;
    }

    // подключение обратных ссылок
    // client->m_conn_p = con;
    client_add_connection(client, con);
    submem_add_connection(sub, con);
    server_add_connection(server, con);

    INF("Client %d connected to server '%s'", client->m_id, server->m_name);
    return 0;

falied_create_con:

    return -ENOMEM;
}

// добавить подключение
void server_add_connection(struct server_t *srv, struct connection_t *con)
{
    // проверка на существование подключения
    if (!con | !srv)
    {
        ERR("there is no connection or server object");
        return;
    }

    mutex_lock(&srv->m_lock);
    // создание объекта списка соединения
    struct serv_conn_list_t *s_con = kmalloc(sizeof(*s_con), GFP_KERNEL);
    if (!s_con)
    {
        ERR("Failed to allocate memory for serv_conn_list_t");
        mutex_unlock(&srv->m_lock);
        return;
    }

    s_con->conn = con;
    INIT_LIST_HEAD(&s_con->list);

    // блокировка списка соединений сервера для добавления нового соединения
    mutex_lock(&srv->m_con_list_lock);
    list_add(&s_con->list, &srv->connection_list.list);
    mutex_unlock(&srv->m_con_list_lock);
    mutex_unlock(&srv->m_lock);
}

// удалить подключение
void server_delete_connection(struct server_t *srv, struct serv_conn_list_t *con)
{
    // проверка на существование подключения
    if (!con | !srv)
    {
        ERR("there is no connection or server object");
        return;
    }

    // глобальная блокировка серверов для удаления подключения
    if (con->conn)
    {
        con->conn->m_server_p = NULL;
        delete_connection(con->conn);
        INF("server disconnected");
    }
    else
        INF("server doesnt have con ptr");

    list_del(&con->list);
    kfree(con);
}

struct serv_conn_list_t *server_find_conn_by_sub_mem_id(struct server_t *srv, int sub_mem_id)
{
    // проверяем входные данные
    if (!srv || !IS_ID_VALID(sub_mem_id))
    {
        ERR("Invalid input params");
        return NULL;
    }
    INF("Finding connection in server (ID: %d)(NAME: %s) with (SUB MEM ID: %d)", srv->m_id, srv->m_name, sub_mem_id);

    // соединение с клиентом и памятью
    struct serv_conn_list_t *conn = NULL;

    mutex_lock(&srv->m_lock);
    mutex_lock(&srv->m_con_list_lock);
    // Итерируемся по списку подключений
    list_for_each_entry(conn, &srv->connection_list.list, list)
    {
        if (conn && conn->conn && conn->conn->m_mem_p && conn->conn->m_mem_p->m_id == sub_mem_id)
        {
            // Нашли совпадение - сохраняем результат
            mutex_unlock(&srv->m_con_list_lock);
            mutex_unlock(&srv->m_lock);
            return conn;
        }
    }
    mutex_unlock(&srv->m_con_list_lock);
    mutex_unlock(&srv->m_lock);
    INF("Connection not found in server (ID: %d)(NAME: %s) with (SUB MEM ID: %d)", srv->m_id, srv->m_name, sub_mem_id);
    return NULL;
}

struct serv_conn_list_t *server_find_conn(struct server_t *srv, struct connection_t *con)
{
    // проверяем входные данные
    if (!srv || !con)
    {
        ERR("Invalid input params");
        return NULL;
    }
    INF("Finding connection in server (ID: %d)(NAME: %s)", srv->m_id, srv->m_name);

    // соединение с клиентом и памятью
    struct serv_conn_list_t *conn = NULL;

    mutex_lock(&srv->m_lock);
    mutex_lock(&srv->m_con_list_lock);
    // Итерируемся по списку подключений
    list_for_each_entry(conn, &srv->connection_list.list, list)
    {
        if (conn && conn->conn && conn->conn == con)
        {
            // Нашли совпадение - сохраняем результат
            INF("Connection found");
            mutex_unlock(&srv->m_con_list_lock);
            mutex_unlock(&srv->m_lock);
            return conn;
        }
    }
    mutex_unlock(&srv->m_con_list_lock);
    mutex_unlock(&srv->m_lock);
    INF("Connection not found in server (ID: %d)(NAME: %s)", srv->m_id, srv->m_name);
    return NULL;
}

void server_get_data(struct server_t *srv, struct st_server *dest)
{
    if (!srv || !dest)
    {
        ERR("Inval param");
        return;
    }

    INF("Getting data from server");
    
    mutex_lock(&srv->m_lock);
    
    dest->id = srv->m_id;
    strncpy(dest->name, srv->m_name, MAX_SERVER_NAME);
    dest->name[MAX_SERVER_NAME-1] = '\0';
    dest->conn_count = 0;

    INF("adding server (ID:%d)(NAME:'%s')", dest->id, dest->name);
    
    // соединение с клиентом и памятью
    struct serv_conn_list_t *conn = NULL;
    
    mutex_lock(&srv->m_con_list_lock);
    // Итерируемся по списку подключений
    list_for_each_entry(conn, &srv->connection_list.list, list)
    {
        if (!conn || !conn->conn || !conn->conn->m_client_p)
        {
            ERR("Incorrect connection data");
            goto get_data_failed;
        }

        if (dest->conn_count >= MAX_CLIENTS_PER_SERVER)
        {
            INF("Too much clients per server in server (NAME: %s)", srv->m_name);
            goto get_data_failed;
        }

        INF("\tOne more connection with client id: %d", conn->conn->m_client_p->m_id);

        // копируем данные о соединениях
        dest->conn_ids[dest->conn_count] = conn->conn->m_client_p->m_id;
        dest->conn_count++;
    }

get_data_failed:
    mutex_unlock(&srv->m_con_list_lock);
    mutex_unlock(&srv->m_lock);
    return;
}

/**
 * Операции над глобальным списком серверов
 */

// удаление списка
void delete_server_list()
{
    mutex_lock(&g_servers_lock);
    struct server_t *server, *server_tmp;
    list_for_each_entry_safe(server, server_tmp, &g_servers_list, list) server_destroy(server);
    mutex_unlock(&g_servers_lock);
}