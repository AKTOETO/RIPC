#include "server.h"
#include "client.h"
#include "err.h"
#include "shm.h"

#include <linux/mm.h>     // операции с памятью
#include <linux/string.h> // операции над строками
#include <linux/sched.h>  // для current

// Список соединений и его блокировка
LIST_HEAD(g_servers_list);
DEFINE_MUTEX(g_servers_lock);

// генератор id для списка клиентов
// DEFINE_ID_GENERATOR(g_servers_id_gen);

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

    // выделние и проверка памяти
    struct server_t *srv = kmalloc(sizeof(*srv), GFP_KERNEL);
    if (!srv)
    {
        ERR("server_create: Cant allocate memory for server");
        return NULL;
    }

    // Инициализация полей
    strscpy(srv->m_name, name, MAX_SERVER_NAME);
    srv->m_id = generate_id(&g_id_gen);
    INIT_LIST_HEAD(&srv->connection_list);
    srv->m_task_p = current;

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

// удаление сервера
void server_destroy(struct server_t *srv)
{
    // проверка входного параметра
    if (!srv)
    {
        ERR("server_destroy: Attempt to destroy NULL server");
        return;
    }

    INF("Destroying server '%s' (ID: %d)", srv->m_name, srv->m_id);

    // для безопасного удаления
    struct connection_t *con, *tmp;

    // Удаление списка клиентов
    mutex_lock(&srv->m_con_list_lock);
    list_for_each_entry_safe(con, tmp, &srv->connection_list, list)
    {
        // удаление из списка подключенных клиентов к серверу
        list_del(&con->list);

        // удаление из общего списка и очистка памяти
        delete_connection(con);
    }
    mutex_unlock(&srv->m_con_list_lock);

    // удаление сервера из глобального списка
    mutex_unlock(&g_servers_lock);
    list_del(&srv->list);
    mutex_unlock(&g_servers_lock);

    free_id(&g_id_gen, srv->m_id);
    kfree(srv);
}

// поиск сервера по имени
struct server_t *find_server_by_name(const char *name)
{
    struct server_t *srv = NULL;
    list_for_each_entry(srv, &g_servers_list, list)
    {
        if (strcmp(srv->m_name, name) == 0)
        {
            return srv;
        }
    }
    return NULL;
}

// поиск сервера
struct server_t *find_server_by_id_pid(int id, pid_t pid)
{
    // проверка входных данных
    if (id < 0)
    {
        ERR("Incorrect id: %d", id);
        return NULL;
    }

    mutex_lock(&g_servers_lock);

    // проходимся по каждому клиенту и ищем подходящего
    struct server_t *server = NULL;

    // Итерируемся по списку клиентов
    list_for_each_entry(server, &g_clients_list, list)
    {
        if (server->m_task_p->pid == pid && server->m_id == id)
        {
            mutex_unlock(&g_servers_lock);
            // Нашли совпадение - сохраняем результат
            return server;
        }
    }
    mutex_unlock(&g_servers_lock);

    return NULL;
}

// поиск клиента из списка сервера по task_struct
struct client_t *find_client_by_task_from_server(
    struct task_struct *task, struct server_t *serv)
{
    // проверяем входные данные
    if (!serv || !task)
    {
        ERR("Invalid input params");
        return NULL;
    }

    // соединение с клиентом и памятью
    struct connection_t *conn = NULL;

    mutex_lock(&serv->m_con_list_lock);
    // Итерируемся по списку подключений
    list_for_each_entry(conn, &serv->connection_list, list)
    {
        if (conn->m_client_p->m_task_p == task)
        {
            // Нашли совпадение - сохраняем результат
            mutex_unlock(&serv->m_con_list_lock);
            return conn->m_client_p;
        }
    }
    mutex_unlock(&serv->m_con_list_lock);
    return NULL;
}

// добавление клиента к серверу
int connect_client_to_server(struct server_t *server, struct client_t *client)
{
    // общая память
    struct shm_t *shm = NULL;

    // проверяем уже существующие подключения между двумя процессами,
    // в которых работают server и client
    struct client_t *client2 = find_client_by_task_from_server(client->m_task_p, server);

    // если существует клиент из того же процесса, с которого пытается подключиться
    // еще один клиент, то просто даем еще одному клиенту ту же область памяти
    if (client2)
        shm = client2->m_conn_p->m_mem_p;

    // если же нет общей памяти у сервера с процессом,
    // из которого к нему подключается клиент, то создаем ее
    else
    {
        shm = shm_create(SHARED_MEM_SIZE);

        // проверка создания памяти
        if (!shm)
        {
            ERR("CONNECT_TO_SERVER: cant create shared memory for client %d and server %s",
                client->m_id, server->m_name);
            return -ENOMEM;
        }
    }

    // создаем объект соединения
    struct connection_t *con = create_connection(client, server, shm);

    // проверка создания объекта соединения
    if (!con)
    {
        ERR("CONNECT_TO_SERVER: failed to create connection_t object");
        return -ENOMEM;
    }

    // подключение обратных ссылок
    client->m_conn_p = con;
    server_add_connection(server, con);
    // увеличиваем счетчик подключенных клиентов к памяти
    atomic_inc(&shm->m_num_of_conn);

    
    INF("Client %d connected to server '%s'", client->m_id, server->m_name);
    return 0;
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

    // блокировка списка соединений сервера для добавления нового соединения
    mutex_lock(&srv->m_con_list_lock);
    list_add(&con->list, &srv->connection_list);
    mutex_unlock(&srv->m_con_list_lock);
}

// удалить подключение
void server_delete_connection(struct server_t *srv, struct connection_t *con)
{
    // проверка на существование подключения
    if (!con | !srv)
    {
        ERR("there is no connection or server object");
        return;
    }

    // глобальная блокировка серверов для добавления в список соединений
    mutex_lock(&srv->m_con_list_lock);
    list_del(&con->list);
    delete_connection(con);
    mutex_unlock(&srv->m_con_list_lock);
}

/**
 * Операции над глобальным списком серверов
 */

// удаление списка
void delete_server_list()
{
    struct server_t *server, *server_tmp;
    list_for_each_entry_safe(server, server_tmp, &g_servers_list, list)
        server_destroy(server);

    // удаление генератора id
    // DELETE_ID_GENERATOR(&g_servers_id_gen);
}