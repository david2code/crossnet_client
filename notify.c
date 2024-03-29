#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <pthread.h>

#include "misc.h"
#include "log.h"
#include "notify.h"
#include "buff.h"

#if 1

struct buff_table g_notify_buff_table;

void notify_buf_table_init()
{
    buff_table_init(&g_notify_buff_table, NOTIFY_NODE_MAX_NUM, sizeof(struct notify_node), "g_notify_buff_table");
    DBG_PRINTF(DBG_NORMAL, "%s ok\n", __FUNCTION__);
}

inline struct notify_node *malloc_notify_node()
{
    return (struct notify_node *)buff_table_malloc_node(&g_notify_buff_table);
}

inline void free_notify_node(struct notify_node *p_node)
{
    buff_table_free_node(&g_notify_buff_table, &p_node->list_head);
}

void display_g_notify_buff_table()
{
    display_buff_table(&g_notify_buff_table);
}

#endif

#if 2

int notify_table_init(struct notify_table *p_table, char *name, uint32_t limit_size)
{
    INIT_LIST_HEAD(&p_table->list_head);
    p_table->list_num = 0;
    pthread_mutex_init(&p_table->mutex, NULL);
    strncpy(p_table->table_name, name, TABLE_NAME_LEN);
    p_table->limit_size    = NOTIFY_NODE_MAX_NUM;

    DBG_PRINTF(DBG_WARNING, "init %s ok, size: %d\n", p_table->table_name, sizeof(struct notify_table));
    return 0;
}

struct notify_node *notify_table_get(struct notify_table *p_table)
{
    struct list_head *p_node = NULL;

    pthread_mutex_lock(&p_table->mutex);

    if (!list_empty(&p_table->list_head)) {
        p_node = p_table->list_head.next;
        list_del(p_node);
        p_table->list_num--;
    }

    pthread_mutex_unlock(&p_table->mutex);

    return (struct notify_node *)p_node;
}

int notify_table_put_head(struct notify_table *p_table, struct notify_node *p_node)
{
    pthread_mutex_lock(&p_table->mutex);

    list_add_fe(&p_node->list_head, &p_table->list_head);
    p_table->list_num++;

    pthread_mutex_unlock(&p_table->mutex);

    return 0;
}

int notify_table_put_tail(struct notify_table *p_table, struct notify_node *p_node)
{
    pthread_mutex_lock(&p_table->mutex);

    list_add_tail(&p_node->list_head, &p_table->list_head);
    p_table->list_num++;

    pthread_mutex_unlock(&p_table->mutex);

    return 0;
}

#endif
