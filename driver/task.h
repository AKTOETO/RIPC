#ifndef REG_TASK_H
#define REG_TASK_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/wait.h>

#include "client.h"
#include "connection.h"
#include "err.h"
#include "ripc.h"
#include "server.h"

/**
 * Глобальные переменные
 */
// Список зарегистрированных процессов и его блокировка
extern struct list_head g_reg_task_list;
extern atomic_t g_reg_task_count;
extern struct mutex g_reg_task_lock;

/**
 * Структура отправленного уведомления
 */
struct notification_t
{
    struct notification_data data;
    struct list_head list;
};

/**
 * Функции для работы со структурой уведомления
 */

// Создание уведомления
struct notification_t *notification_create(enum notif_sender sender, enum notif_type type, int sub_mem_id,
                                           int sender_id, int reciver_id);

// Удаление уведомления
void notification_delete(struct notification_t *notif);

// отправление уведомления
int notification_send(enum notif_sender sender, enum notif_type type, struct connection_t *con);

/**
 * структура для добавления сервера в список
 */
struct servers_list_t
{
    struct reg_task_t *m_reg_task;
    struct server_t *m_server;
    struct list_head list;
};

/**
 * Методы возаимоедйствия со структурой связи
 */

// создание объекта связи
struct servers_list_t *servers_list_t_create(struct reg_task_t *reg_task, struct server_t *serv);

// удаление объекта связи
void servers_list_t_delete(struct servers_list_t *srv_lst_entry);

/**
 *   структура для добавления клиента в список
 */
struct clients_list_t
{
    struct reg_task_t *m_reg_task;
    struct client_t *m_client;
    struct list_head list;
};

/**
 * Методы возаимоедйствия со структурой связи
 */
// создание объекта связи
struct clients_list_t *clients_list_t_create(struct reg_task_t *reg_task, struct client_t *cli);

// удаление объекта связи
void clients_list_t_delete(struct clients_list_t *cli_lst_entry);

/**
 * Структура регистрации подключенных процессов
 */
struct reg_task_t
{
    struct task_struct *m_task_p;
    atomic_t m_is_monitor;          // Является ли процесс утилитой мониторинга ripcctl
    struct list_head m_notif_list;  // список уведомлений
    struct mutex m_notif_list_lock; // блокировка доступа к списку уведомлений
    atomic_t m_num_of_notif;        // количество уведомлений в списке
    struct list_head m_servers;     // список серверов
    atomic_t m_num_of_servers;      // количество серверов в процессе
    struct list_head m_clients;     // список клиентов
    atomic_t m_num_of_clients;      // Количество кдиентов в процессе
    wait_queue_head_t m_wait_queue; // для блокировки процесса при poll ожидании
    struct mutex m_wait_queue_lock; // блокировка доступа к очереди ожидания
    struct list_head list;
};

/**
 * Функции работы с регистрацией процессов
 */

/**
 * @brief Проверка возможности регистрации процесса
 *
 * @return int 1 - можно регистрировать, 0 - нельзя
 */
int reg_task_can_add_task(void);

// Создание зарегистрированного процесса
struct reg_task_t *reg_task_create(void);

// Удаление зарегистрированного процесса
void reg_task_delete(struct reg_task_t *reg_task);

// поиск по task_struct
struct reg_task_t *reg_task_find_by_task_struct(struct task_struct *task);

// проверка возможности добавления сервера
int reg_task_can_add_server(struct reg_task_t *reg_task);

// проверка возможности добавления клиента
int reg_task_can_add_client(struct reg_task_t *reg_task);

// добавление сервера
void reg_task_add_server(struct reg_task_t *reg_task, struct server_t *serv);

// добавление клиента
void reg_task_add_client(struct reg_task_t *reg_task, struct client_t *cli);

// удаление клиента
void reg_task_delete_client(struct clients_list_t *cli);

// удаление сервера
void reg_task_delete_server(struct servers_list_t *srv);

// Установка монитора
int reg_task_set_monitor(struct reg_task_t *reg_task);

/**
 * @brief Является ли reg_task монитором (Ripcctl)
 * @param reg_task указатель на задачу
 * @return int 1 - монитор, 0 - нет
 */
int reg_task_is_monitor(struct reg_task_t *reg_task);

/**
 * Работа с уведомлениями
 */

// получение размера очереди уведомлений
int reg_task_get_notif_count(struct reg_task_t *reg_task);

// добавление уведомления
int reg_task_add_notification(struct reg_task_t *reg_task, struct notification_t *notif);

// получение уведомления
struct notification_t *reg_task_get_notification(struct reg_task_t *reg_task);

// есть ли сообщения в очереди
int reg_task_is_notif_pending(struct reg_task_t *reg_task);

// пробуждение очереди ожидания
void reg_task_notify_all(struct reg_task_t *reg_task);

/**
 * Работа со структурой отправки данных
 */
void reg_task_get_data(struct st_reg_tasks *reg_tasks);

#endif // !REG_TASK_H