#include "task.h"
#include "id_pack.h"

#include <linux/pid.h> // pid_alive
#include <linux/mm.h>

// Список соединений и его блокировка
LIST_HEAD(g_reg_task_list);
DEFINE_MUTEX(g_reg_task_lock);

struct notification_t *notification_create(
    enum notif_sender who_sends, enum notif_type type,
    int sub_mem_id, int sender_id, int reciver_id)
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
        ERR("some id is not valid (SUB_MEM_ID:%d)(SENDER_ID:%d)(RECIVER_ID:%d)",
            sub_mem_id, sender_id, reciver_id);
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

    INF("Created notif: (TYPE:%d)(WHO_SENDS:%d)(SUB_MEM_ID:%d)(SENDER_ID:%d)(RECIVER_ID:%d)",
        type, who_sends, sub_mem_id, sender_id, reciver_id);

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

    INF("Deleting notif: (TYPE:%d)(WHO_SENDS:%d)(SUB_MEM_ID:%d)(SENDER_ID:%d)(RECIVER_ID:%d)",
        notif->data.m_type,
        notif->data.m_who_sends,
        notif->data.m_sub_mem_id,
        notif->data.m_sender_id,
        notif->data.m_reciver_id);

    // удаление уведомления из списка в зарегистированной структуре
    list_del(&notif->list);

    kfree(notif);
}

int notification_send(enum notif_sender sender,
                      enum notif_type type,
                      struct connection_t *con)
{
    if (!IS_NTF_SEND_VALID(sender) || !IS_NTF_TYPE_VALID(type) || !con)
    {
        ERR("Param error");
        return -ENOPARAM;
    }

    int sub_mem_id = con->m_mem_p->m_id;
    int sender_id, reciever_id;
    struct reg_task_t *reciever_task;

    // определяем отправителя
    switch (sender)
    {
    case CLIENT:
        sender_id = con->m_client_p->m_id;
        reciever_id = con->m_server_p->m_id;
        reciever_task = con->m_server_p->m_task_p->m_reg_task;
        break;

    case SERVER:
        sender_id = con->m_server_p->m_id;
        reciever_id = con->m_client_p->m_id;
        reciever_task = con->m_client_p->m_task_p->m_reg_task;
        break;
    default:
        ERR("Undefined sender type %d", sender);
    }

    // создание уведомления
    struct notification_t *ntf = notification_create(
        sender, type, sub_mem_id, sender_id, reciever_id);
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
            INF("Notification sent to server (ID:%d)(PID:%d)(NAME:%s)",
                reciever_id,
                reciever_task->m_task_p->pid,
                con->m_server_p->m_name);
            break;
            // отправил сервер, то есть полцчатель будет клиент
        case SERVER:
            INF("Notification sent to client (ID:%d)(PID:%d)",
                reciever_id,
                reciever_task->m_task_p->pid);

            break;
        default:
            ERR("Undefined sender type: %d", sender);
        }
    }
    else
    {
        ERR("notification hasnt been added");
        return -EFAULT;
    }

    return 0;
}

struct servers_list_t *servers_list_t_create(
    struct reg_task_t *reg_task, struct server_t *serv)
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

void servers_list_t_delete(
    struct servers_list_t *srv_lst_entry)
{
    if (!srv_lst_entry)
    {
        ERR("empty servers_list_t entry");
        return;
    }

    kfree(srv_lst_entry);

    INF("Deleted servers_list_t");
}

struct clients_list_t *clients_list_t_create(
    struct reg_task_t *reg_task, struct client_t *cli)
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

void clients_list_t_delete(
    struct clients_list_t *cli_lst_entry)
{
    if (!cli_lst_entry)
    {
        ERR("empty clients_list_t entry");
        return;
    }

    kfree(cli_lst_entry);

    INF("Deleted clients_list_t");
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
    INIT_LIST_HEAD(&reg_task->list);
    INIT_LIST_HEAD(&reg_task->m_clients);
    INIT_LIST_HEAD(&reg_task->m_notif_list);
    INIT_LIST_HEAD(&reg_task->m_servers);
    init_waitqueue_head(&reg_task->m_wait_queue);
    reg_task->m_task_p = task;

    // добавление в глобальный список
    mutex_lock(&g_reg_task_lock);
    list_add(&reg_task->list, &g_reg_task_list);
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
        // Удаляем из списка reg_task
        list_del(&srv_entry->list);
        if (srv_entry->m_server)
        {
            // Удаляем сервер (из глоб. списка и kfree)
            server_destroy(srv_entry->m_server);
        }
        // Освобождаем элемент списка reg_task
        kfree(srv_entry);
    }

    // Удалить сами структуры клиентов
    list_for_each_entry_safe(cli_entry, cli_tmp, &reg_task->m_clients, list)
    {
        list_del(&cli_entry->list);
        if (cli_entry->m_client)
        {
            client_destroy(cli_entry->m_client);
        }
        kfree(cli_entry);
    }

    // // удаляем список серверов
    // if (!list_empty(&reg_task->m_servers))
    // {
    //     INF("Deleting servers");
    //     struct servers_list_t *srv, *srv_tmp;
    //     list_for_each_entry_safe(srv, srv_tmp, &reg_task->m_servers, list)
    //     {
    //         if (srv->m_server)
    //             server_destroy(srv->m_server);
    //         else
    //             ERR("Empty server ptr");
    //         servers_list_t_delete(srv);
    //     }
    // }

    // // удаляем список клиентов
    // if (!list_empty(&reg_task->m_clients))
    // {
    //     INF("Deleting clients");
    //     struct clients_list_t *cli, *cli_tmp;
    //     list_for_each_entry_safe(cli, cli_tmp, &reg_task->m_clients, list)
    //     {
    //         if (cli->m_client)
    //             client_destroy(cli->m_client);
    //         else
    //             ERR("Empty client ptr");
    //         clients_list_t_delete(cli);
    //     }
    // }

    // удаляем список уведомлений
    if (!list_empty(&reg_task->m_notif_list))
    {
        INF("Deleting notifications");
        struct notification_t *notif, *notif_tmp;
        list_for_each_entry_safe(notif, notif_tmp, &reg_task->m_notif_list, list)
        {
            notification_delete(notif);
        }
    }

    if (reg_task->m_task_p)
    {
        put_task_struct(reg_task->m_task_p);
        reg_task->m_task_p = NULL;
    }
    mutex_lock(&g_reg_task_lock);
    list_del(&reg_task->list);
    mutex_unlock(&g_reg_task_lock);
    kfree(reg_task);

    INF("Finished cleaning reg_task");

    // // освобождение task_struct
    // put_task_struct(reg_task->m_task_p);
    // reg_task->m_task_p = NULL;

    // // удаление сервера из глобального списка
    // mutex_unlock(&g_reg_task_lock);
    // list_del(&reg_task->list);
    // mutex_unlock(&g_reg_task_lock);

    // kfree(reg_task);
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

void reg_task_add_server(
    struct reg_task_t *reg_task, struct server_t *serv)
{
    if (!reg_task || !serv)
    {
        ERR("empty param");
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

    INF("Server (ID:%d)(NAME:%s) added to task (PID:%d)",
        serv->m_id, serv->m_name, reg_task->m_task_p->pid);
}

void reg_task_add_client(
    struct reg_task_t *reg_task, struct client_t *cli)
{
    if (!reg_task || !cli)
    {
        ERR("empty param");
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

    INF("Client (ID:%d) added to task (PID:%d)", cli->m_id, reg_task->m_task_p->pid);
}

int reg_task_add_notification(
    struct reg_task_t *reg_task, struct notification_t *notif)
{
    if (!reg_task || !notif)
    {
        ERR("Reg_task or notif is NULL");
        return -ENODATA;
    }

    INF("Adding new notification to task (PID:%d)", reg_task->m_task_p->pid);

    list_add(&reg_task->m_notif_list, &notif->list);

    reg_task_notify_all(reg_task);
    return 0;
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

    // получение уведомления
    struct notification_t *notif =
        list_first_entry(&reg_task->m_notif_list, struct notification_t, list);

    if (notif == NULL)
    {
        ERR("Couldnt get notification");
        return NULL;
    }

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
    return !list_empty(&reg_task->m_notif_list);
}

void reg_task_notify_all(struct reg_task_t *reg_task)
{
    if (!reg_task)
    {
        ERR("NULL param");
        return;
    }

    wake_up_interruptible(&reg_task->m_wait_queue);
}
