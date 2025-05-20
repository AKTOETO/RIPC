#include "task.h"
#include "client.h"
#include "err.h"
#include "id_pack.h"
#include "ripc.h"
#include "server.h"

#include <linux/mm.h>
#include <linux/pid.h> // pid_alive

// Список соединений и его блокировка
LIST_HEAD(g_reg_task_list);
DEFINE_MUTEX(g_reg_task_lock);
atomic_t g_reg_task_count = ATOMIC_INIT(0);

struct notification_t *notification_create(enum notif_sender who_sends, enum notif_type type, int sub_mem_id,
                                           int sender_id, int reciver_id)
{
    // проверка типа отправителя
    if (!IS_NTF_SEND_VALID(who_sends))
    {
        ERR("unknown sender type %d", who_sends);
        return NULL;
    }

    // проверка типа уведомления
    if (!IS_NTF_TYPE_VALID(type))
    {
        ERR("unknown type %d", type);
        return NULL;
    }

    // проверка всех идентификаторов
    if (!IS_ID_VALID(sender_id) || !IS_ID_VALID(sub_mem_id) || !IS_ID_VALID(reciver_id))
    {
        ERR("some id is not valid (SUB_MEM_ID:%d)(SENDER_ID:%d)(RECIVER_ID:%d)", sub_mem_id, sender_id, reciver_id);
        return NULL;
    }

    // создание объекта уведомления
    struct notification_t *notif = kmalloc(sizeof(*notif), GFP_KERNEL);
    if (!notif)
    {
        ERR("Cant allocate memory for notification");
        return NULL;
    }

    // инициализация полей
    notif->data.m_reciver_id = reciver_id;
    notif->data.m_sender_id = sender_id;
    notif->data.m_sub_mem_id = sub_mem_id;
    notif->data.m_who_sends = who_sends;
    notif->data.m_type = type;
    INIT_LIST_HEAD(&notif->list);

    INF("Created notif: (TYPE:%d)(WHO_SENDS:%d)(SUB_MEM_ID:%d)(SENDER_ID:%d)(RECIVER_ID:%d)", type, who_sends,
        sub_mem_id, sender_id, reciver_id);

    return notif;
}

void notification_delete(struct notification_t *notif)
{
    // проверка входного параметра
    if (!notif)
    {
        ERR("Attempt to destroy NULL notif");
        return;
    }

    INF("Deleting notif: (TYPE:%d)(WHO_SENDS:%d)(SUB_MEM_ID:%d)(SENDER_ID:%d)(RECIVER_ID:%d)", notif->data.m_type,
        notif->data.m_who_sends, notif->data.m_sub_mem_id, notif->data.m_sender_id, notif->data.m_reciver_id);

    kfree(notif);
}

int notification_send(enum notif_sender sender, enum notif_type type, struct connection_t *con)
{
    if (!IS_NTF_SEND_VALID(sender))
    {
        ERR("Invalid sender");
        return -ENOPARAM;
    }

    if (!IS_NTF_TYPE_VALID(type))
    {
        ERR("Invalid type");
        return -ENOPARAM;
    }
    if (!con)
    {
        ERR("NULL connection");
        return -ENOPARAM;
    }
    if (!con->m_mem_p)
    {
        ERR("NULL con->mem");
        return -ENOPARAM;
    }

    int sub_mem_id = con->m_mem_p->m_id;
    int sender_id, reciever_id;
    struct reg_task_t *reciever_task;

    // определяем отправителя
    switch (sender)
    {
    case CLIENT:
        if (!con->m_client_p)
        {
            ERR("No client ptr in connection");
            return -ENOENT;
        }
        sender_id = (!con->m_client_p ? -1 : con->m_client_p->m_id);
        reciever_id = (!con->m_server_p ? -1 : con->m_server_p->m_id);
        reciever_task = (!con->m_server_p ? NULL : con->m_server_p->m_task_p->m_reg_task);
        break;

    case SERVER:
        if (!con->m_server_p)
        {
            ERR("No client ptr in connection");
            return -ENOENT;
        }
        sender_id = (!con->m_server_p ? -1 : con->m_server_p->m_id);
        reciever_id = (!con->m_client_p ? -1 : con->m_client_p->m_id);
        reciever_task = (!con->m_client_p ? NULL : con->m_client_p->m_task_p->m_reg_task);
        break;
    default:
        ERR("Undefined sender type %d", sender);
    }

    // создание уведомления
    struct notification_t *ntf = notification_create(sender, type, sub_mem_id, sender_id, reciever_id);
    if (!ntf)
    {
        ERR("Notif hasnt created");
        return -EFAULT;
    }

    // добавляем к процессу уведомление
    if (!reg_task_add_notification(reciever_task, ntf))
    {
        switch (sender)
        {
        // отправил клиент, то есть полцчатель будет сервер
        case CLIENT:
            INF("Notification sent to server (ID:%d)(PID:%d)(NAME:%s)", reciever_id, reciever_task->m_task_p->pid,
                con->m_server_p->m_name);
            break;
            // отправил сервер, то есть полцчатель будет клиент
        case SERVER:
            INF("Notification sent to client (ID:%d)(PID:%d)", reciever_id, reciever_task->m_task_p->pid);

            break;
        default:
            ERR("Undefined sender type: %d", sender);
        }
    }
    else
    {
        ERR("notification hasnt been added");
        notification_delete(ntf);
        return -EFAULT;
    }

    return 0;
}

struct servers_list_t *servers_list_t_create(struct reg_task_t *reg_task, struct server_t *serv)
{
    if (!reg_task || !serv)
    {
        ERR("Empty param");
        return NULL;
    }

    // выделяем память под структуру
    struct servers_list_t *srv_entr = kmalloc(sizeof(*srv_entr), GFP_KERNEL);
    if (!srv_entr)
    {
        ERR("Cant allocate memory for servers_list_t");
        return NULL;
    }

    // инициализация полей
    INIT_LIST_HEAD(&srv_entr->list);
    srv_entr->m_reg_task = reg_task;
    srv_entr->m_server = serv;

    INF("Created servers_list_t");

    return srv_entr;
}

void servers_list_t_delete(struct servers_list_t *srv_lst_entry)
{
    if (!srv_lst_entry)
    {
        ERR("empty servers_list_t entry");
        return;
    }

    kfree(srv_lst_entry);

    INF("Deleted servers_list_t");
}

struct clients_list_t *clients_list_t_create(struct reg_task_t *reg_task, struct client_t *cli)
{
    if (!reg_task || !cli)
    {
        ERR("Empty param");
        return NULL;
    }

    // выделяем память под структуру
    struct clients_list_t *cli_entr = kmalloc(sizeof(*cli_entr), GFP_KERNEL);
    if (!cli_entr)
    {
        ERR("Cant allocate memory for clients_list_t");
        return NULL;
    }

    // инициализация полей
    INIT_LIST_HEAD(&cli_entr->list);
    cli_entr->m_reg_task = reg_task;
    cli_entr->m_client = cli;

    INF("Created clients_list_t");

    return cli_entr;
}

void clients_list_t_delete(struct clients_list_t *cli_lst_entry)
{
    if (!cli_lst_entry)
    {
        ERR("empty clients_list_t entry");
        return;
    }

    kfree(cli_lst_entry);

    INF("Deleted clients_list_t");
}

int reg_task_get_notif_count(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("task ptr is null");
        return -1;
    }

    return atomic_read(&reg_task->m_num_of_notif);
}

int reg_task_can_add_task(void)
{
    return (atomic_read(&g_reg_task_count) < MAX_PROCESSES);
}

struct reg_task_t *reg_task_create(void)
{
    struct task_struct *task = current;

    // Проверка входных данных
    if (!task || !pid_alive(task))
    {
        ERR("Task ptr is NULL or dead");
        return NULL;
    }

    // зарегистрирован ли уже этот процесс
    if (reg_task_find_by_task_struct(task))
    {
        ERR("Task already registered (PID:%d)", task->pid);
        return NULL;
    }

    // увеличиваем счётчик ссылок
    get_task_struct(task);

    // выделение и проверка памяти
    struct reg_task_t *reg_task = kmalloc(sizeof(*reg_task), GFP_KERNEL);
    if (!reg_task)
    {
        ERR("Cant allocate memory for reg_task");
        return NULL;
    }

    // инициализация полей
    atomic_set(&reg_task->m_is_monitor, 0);
    INIT_LIST_HEAD(&reg_task->list);
    INIT_LIST_HEAD(&reg_task->m_clients);
    INIT_LIST_HEAD(&reg_task->m_notif_list);
    mutex_init(&reg_task->m_notif_list_lock);
    atomic_set(&reg_task->m_num_of_notif, 0);
    atomic_set(&reg_task->m_num_of_servers, 0);
    atomic_set(&reg_task->m_num_of_clients, 0);
    INIT_LIST_HEAD(&reg_task->m_servers);
    init_waitqueue_head(&reg_task->m_wait_queue);
    mutex_init(&reg_task->m_wait_queue_lock);
    reg_task->m_task_p = task;

    // добавление в глобальный список
    mutex_lock(&g_reg_task_lock);
    list_add(&reg_task->list, &g_reg_task_list);
    atomic_inc(&g_reg_task_count);
    mutex_unlock(&g_reg_task_lock);

    INF("Created reg_task: (PID:%d)", task->pid);

    return reg_task;
}

void reg_task_delete(struct reg_task_t *reg_task)
{

    // проверка входного параметра
    if (!reg_task)
    {
        ERR("Attempt to destroy NULL reg_task");
        return;
    }

    if (!reg_task->m_task_p || !pid_alive(reg_task->m_task_p))
    {
        ERR("There is no task ptr in reg_task");
        return;
    }

    INF("Destroying reg_tasks (PID:%d)", reg_task->m_task_p->pid);

    struct servers_list_t *srv_entry, *srv_tmp;
    struct clients_list_t *cli_entry, *cli_tmp;

    // Очистить СОЕДИНЕНИЯ для серверов этого процесса
    // (Это вызовет delete_connection и освободит sub_mem)
    list_for_each_entry(srv_entry, &reg_task->m_servers, list)
    {
        if (srv_entry->m_server)
        {
            server_cleanup_connections(srv_entry->m_server);
        }
    }

    // Очистить СОЕДИНЕНИЕ для клиентов этого процесса
    // (Это вызовет delete_connection и освободит sub_mem)
    list_for_each_entry(cli_entry, &reg_task->m_clients, list)
    {
        if (cli_entry->m_client)
        {
            client_cleanup_connection(cli_entry->m_client);
        }
    }

    // Удалить сами структуры серверов
    list_for_each_entry_safe(srv_entry, srv_tmp, &reg_task->m_servers, list)
    {
        reg_task_delete_server(srv_entry);
    }

    // Удалить сами структуры клиентов
    list_for_each_entry_safe(cli_entry, cli_tmp, &reg_task->m_clients, list)
    {
        reg_task_delete_client(cli_entry);
    }

    // удаляем список уведомлений
    if (!list_empty(&reg_task->m_notif_list))
    {
        mutex_lock(&reg_task->m_notif_list_lock);
        INF("Deleting notifications");
        struct notification_t *notif, *notif_tmp;
        list_for_each_entry_safe(notif, notif_tmp, &reg_task->m_notif_list, list)
        {
            atomic_dec(&reg_task->m_num_of_notif);
            notification_delete(notif);
        }
        mutex_unlock(&reg_task->m_notif_list_lock);
    }

    if (reg_task->m_task_p)
    {
        put_task_struct(reg_task->m_task_p);
        reg_task->m_task_p = NULL;
    }
    mutex_lock(&g_reg_task_lock);
    list_del(&reg_task->list);
    atomic_dec(&g_reg_task_count);
    mutex_unlock(&g_reg_task_lock);
    kfree(reg_task);

    INF("Finished cleaning reg_task");
}

struct reg_task_t *reg_task_find_by_task_struct(struct task_struct *task)
{
    if (!task || !pid_alive(task))
    {
        ERR("Task ptr is NULL or dead");
        return NULL;
    }

    struct reg_task_t *entr = NULL;

    mutex_lock(&g_reg_task_lock);
    list_for_each_entry(entr, &g_reg_task_list, list)
    {
        if (entr->m_task_p == task)
        {
            mutex_unlock(&g_reg_task_lock);
            return entr;
        }
    }
    mutex_unlock(&g_reg_task_lock);

    return NULL;
}

// проверка возможности добавления сервера
int reg_task_can_add_server(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("empty param");
        return -ENOPARAM;
    }

    return !reg_task_is_monitor(reg_task) && (atomic_read(&reg_task->m_num_of_servers) < MAX_SERVERS_PER_PID);
}

// проверка возможности добавления клиента
int reg_task_can_add_client(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("empty param");
        return -ENOPARAM;
    }

    return !reg_task_is_monitor(reg_task) && (atomic_read(&reg_task->m_num_of_clients) < MAX_CLIENTS_PER_PID);
}

void reg_task_add_server(struct reg_task_t *reg_task, struct server_t *serv)
{
    if (!reg_task || !serv)
    {
        ERR("empty param");
        return;
    }

    // проверка возможности добавления
    if (!reg_task_can_add_server(reg_task))
    {
        ERR("cannot add server to task (TOO MUCH SERVERS)");
        return;
    }

    // создаем промежуточный объект для связи
    struct servers_list_t *srv_entry = servers_list_t_create(reg_task, serv);
    if (!srv_entry)
    {
        ERR("Cant add server to task");
        return;
    }

    // настроить ссылки
    // reg_task -> servers_list_t
    list_add(&srv_entry->list, &reg_task->m_servers);

    // server_t -> servers_list_t
    server_add_task(serv, srv_entry);

    atomic_inc(&reg_task->m_num_of_servers);

    INF("Server (ID:%d)(NAME:%s) added to task (PID:%d)", serv->m_id, serv->m_name, reg_task->m_task_p->pid);
}

void reg_task_add_client(struct reg_task_t *reg_task, struct client_t *cli)
{
    if (!reg_task || !cli)
    {
        ERR("empty param");
        return;
    }

    // проверка возможности добавления
    if (!reg_task_can_add_client(reg_task))
    {
        ERR("cannot add client to task (TOO MUCH CLIENTS)");
        return;
    }

    // создаем промежуточный объект для связи
    struct clients_list_t *cli_entry = clients_list_t_create(reg_task, cli);
    if (!cli_entry)
    {
        ERR("Cant add client to task");
        return;
    }

    // настроить ссылки
    // reg_task -> clients_list_t
    list_add(&cli_entry->list, &reg_task->m_clients);

    // client_t -> clients_list_t
    client_add_task(cli, cli_entry);

    atomic_inc(&reg_task->m_num_of_clients);

    INF("Client (ID:%d) added to task (PID:%d)", cli->m_id, reg_task->m_task_p->pid);
}

int reg_task_add_notification(struct reg_task_t *reg_task, struct notification_t *notif)
{
    if (!reg_task || !notif)
    {
        ERR("Reg_task or notif is NULL");
        return -ENODATA;
    }

    INF("Adding new notification to task (PID:%d)", reg_task->m_task_p->pid);

    mutex_lock(&reg_task->m_notif_list_lock);
    list_add_tail(&notif->list, &reg_task->m_notif_list);
    atomic_inc(&reg_task->m_num_of_notif);
    mutex_unlock(&reg_task->m_notif_list_lock);

    reg_task_notify_all(reg_task);
    return 0;
}

void reg_task_delete_client(struct clients_list_t *cli_entry)
{
    if (!cli_entry)
    {
        ERR("No param");
        return;
    }

    list_del(&cli_entry->list);
    if (cli_entry->m_client)
    {
        client_destroy(cli_entry->m_client);
        atomic_dec(&cli_entry->m_reg_task->m_num_of_clients);
    }
    kfree(cli_entry);
}

void reg_task_delete_server(struct servers_list_t *srv_entry)
{
    if (!srv_entry)
    {
        ERR("No param");
        return;
    }

    // Удаляем из списка reg_task
    list_del(&srv_entry->list);
    if (srv_entry->m_server)
    {
        // Удаляем сервер (из глоб. списка и kfree)
        server_destroy(srv_entry->m_server);
        atomic_dec(&srv_entry->m_reg_task->m_num_of_servers);
    }
    // Освобождаем элемент списка reg_task
    kfree(srv_entry);
}

int reg_task_set_monitor(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("reg_task is null");
        return -ENODATA;
    }

    // если есть клиенты или серверы в процессе, то регисрация запрещена
    if (atomic_read(&reg_task->m_num_of_clients) > 0 || atomic_read(&reg_task->m_num_of_servers) > 0)
    {
        ERR("cant set monitor type: there are some clients or servers");
        return -EFAULT;
    }

    // Поиск монитора в глобальном списке зарегистрированных процессов
    struct reg_task_t *entr = NULL;
    mutex_lock(&g_reg_task_lock);
    list_for_each_entry(entr, &g_reg_task_list, list)
    {
        if (reg_task_is_monitor(entr))
        {
            mutex_unlock(&g_reg_task_lock);
            return -EEXIST;
        }
    }
    mutex_unlock(&g_reg_task_lock);

    // если же монитор найден не был, значит регистрируем его
    atomic_inc(&reg_task->m_is_monitor);
    return 0;
}

int reg_task_is_monitor(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("reg_task is null");
        return -ENODATA;
    }

    return atomic_read(&reg_task->m_is_monitor);
}

struct notification_t *reg_task_get_notification(struct reg_task_t *reg_task)
{
    int ret = reg_task_is_notif_pending(reg_task);
    // если произошла ошибка
    if (ret == -1)
    {
        ERR("CANT pop notification");
        return NULL;
    }
    // если просто нет уведомлений
    if (ret == 0)
    {
        INF("There is not notification");
        return NULL;
    }

    mutex_lock(&reg_task->m_notif_list_lock);
    // получение уведомления
    struct notification_t *notif = list_first_entry(&reg_task->m_notif_list, struct notification_t, list);
    if (notif == NULL)
    {
        mutex_unlock(&reg_task->m_notif_list_lock);
        ERR("Couldnt get notification");
        return NULL;
    }
    atomic_dec(&reg_task->m_num_of_notif);

    // удаление уведомления из списка в зарегистированной структуре
    list_del(&notif->list);
    mutex_unlock(&reg_task->m_notif_list_lock);

    return notif;
}

int reg_task_is_notif_pending(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("NULL param");
        return -1;
    }

    // если что-то есть в списке,
    // значит еще есть не отправленные уведомления
    mutex_lock(&reg_task->m_notif_list_lock);
    int res = !list_empty(&reg_task->m_notif_list);
    mutex_unlock(&reg_task->m_notif_list_lock);
    return res;
}

void reg_task_notify_all(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("NULL param");
        return;
    }

    mutex_lock(&reg_task->m_wait_queue_lock);
    wake_up_interruptible(&reg_task->m_wait_queue);
    mutex_unlock(&reg_task->m_wait_queue_lock);
}

void reg_task_get_data(struct st_reg_tasks *reg_tasks)
{
    if (!reg_tasks)
    {
        ERR("null params");
        return;
    }

    reg_tasks->tasks_count = 0;

    // нужно пройти по всему списку задач и скопировать информацию о них в структуру
    struct reg_task_t *entr = NULL;
    mutex_lock(&g_reg_task_lock);
    list_for_each_entry(entr, &g_reg_task_list, list)
    {
        if (!entr)
        {
            ERR("NULL reg_task_entry");
            mutex_unlock(&g_reg_task_lock);
            return;
        }
        INF("Got a new task PID: %d", entr->m_task_p->pid);

        if (reg_tasks->tasks_count >= MAX_PROCESSES)
        {
            INF("Too much processes in global list");
            mutex_unlock(&g_reg_task_lock);
            return;
        }

        struct st_task *cur_task = &reg_tasks->tasks[reg_tasks->tasks_count];

        // собираем информацию о количестве клиентов и серверов
        // reg_tasks->tasks[reg_tasks->tasks_count].servers_count = 0; // atomic_read(&entr->m_num_of_servers);
        // reg_tasks->tasks[reg_tasks->tasks_count].pid = entr->m_task_p->pid;
        // reg_tasks->tasks[reg_tasks->tasks_count].clients_count = 0; // atomic_read(&entr->m_num_of_clients);

        cur_task->servers_count = 0;
        cur_task->clients_count = 0;
        cur_task->pid = 0;

        // собираем информацию о каждом сервере
        struct servers_list_t *srv_entr = NULL;
        list_for_each_entry(srv_entr, &entr->m_servers, list)
        {
            if (!srv_entr || !srv_entr->m_server)
            {
                ERR("NULL server entry!!!");
                mutex_unlock(&g_reg_task_lock);
                return;
            }
            INF("Got a new server %d'%s' in task PID: %d", srv_entr->m_server->m_id, srv_entr->m_server->m_name, entr->m_task_p->pid);

            mutex_unlock(&g_reg_task_lock);

            server_get_data(srv_entr->m_server, &cur_task->servers[cur_task->servers_count]);
            cur_task->servers_count++;

            mutex_lock(&g_reg_task_lock);
            // reg_tasks->tasks[reg_tasks->tasks_count].servers_count++;
        }

        // собираем информацию о каждом клиенте
        struct clients_list_t *cli_entr = NULL;
        list_for_each_entry(cli_entr, &entr->m_clients, list)
        {
            if (!cli_entr || !cli_entr->m_client)
            {
                ERR("NULL client entry!!!");
                mutex_unlock(&g_reg_task_lock);
                return;
            }
            INF("Got a new client %d in task PID: %d", cli_entr->m_client->m_id, entr->m_task_p->pid);

            mutex_unlock(&g_reg_task_lock);
            client_get_data(cli_entr->m_client, &cur_task->clients[cur_task->clients_count]);
            // client_get_data(cli_entr, &reg_tasks->tasks[reg_tasks->tasks_count]
            //                                .clients[reg_tasks->tasks[reg_tasks->tasks_count].clients_count]);

            cur_task->clients_count++;
            // reg_tasks->tasks[reg_tasks->tasks_count].clients_count++;
            mutex_lock(&g_reg_task_lock);
        }

        // увеличиваем количество записанных задач
        reg_tasks->tasks_count++;
    }
    INF("Exiting collection data function");
    mutex_unlock(&g_reg_task_lock);
}
