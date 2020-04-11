#ifndef _BACKEND_H
#define _BACKEND_H

#include "kernel_list.h"
#include "hash_table.h"
#include "notify.h"
#include "heaptimer.h"

#define BACKEND_SOCKET_MAX_NUM              5000
#define BACKEND_THREAD_HASH_SIZE            ((BACKEND_SOCKET_MAX_NUM / BACKEND_WORK_THREAD_NUM) / 2)

#define BACKEND_ACCEPT_EPOLL_MAX_EVENTS     (BACKEND_SOCKET_MAX_NUM / 3)
#define BACKEND_ACCEPT_LISTEN_BACKLOG       (BACKEND_ACCEPT_EPOLL_MAX_EVENTS)

#define BACKEND_THREAD_EPOLL_MAX_EVENTS     (BACKEND_ACCEPT_EPOLL_MAX_EVENTS / BACKEND_WORK_THREAD_NUM)
#define BACKEND_THREAD_LISTEN_BACKLOG       (BACKEND_ACCEPT_LISTEN_BACKLOG / BACKEND_WORK_THREAD_NUM)

#define BACKEND_SOCKET_HEART_BEAT_TIMEOUT           10

#define BACKEND_SOCKET_TIMEOUT                        60
#define BACKEND_SOCKET_RECONNECT_TIMEOUT              2
#define BACKEND_SOCKET_CONNECTING_TIMEOUT             5
#define BACKEND_SOCKET_CONNECTED_TIMEOUT              5

#define BACKEND_INNER_SOCKET_CONNECTING_TIMEOUT             5
#define BACKEND_INNER_SOCKET_CONNECTED_TIMEOUT              5

#define BACKEND_NOTIFY_MAX_NODE             (BACKEND_SOCKET_MAX_NUM)

#define BACKEND_HEAP_MAX_SIZE               (BACKEND_SOCKET_MAX_NUM)
#define BACKEND_HEAP_INVALID_HOLE           (BACKEND_HEAP_MAX_SIZE + 1)

enum sk_timer_event {
    STE_INIT,
    STE_CONNECTING,
    STE_CONNECTED,
    STE_MAX
};

struct backend_sk_node {
    struct list_head    list_head;
    struct list_head    id_hash_node;

    int                 fd;
    uint32_t            seq_id;

    uint32_t            ip;
    uint16_t            port;

    uint32_t            user_block_id;

    struct notify_node  *p_recv_node;
    struct list_head    send_list;
    struct backend_sk_node *peer;

    time_t              last_active;
    time_t              last_hb_time; /* last heart beat time */

    uint8_t             type;
    uint8_t             blocked;

    uint32_t            alive_cnt;
    uint32_t            quality;
    uint32_t            delay_ms;

    struct heap_timer   timer;
    enum sk_timer_event event; /*state machine*/

    void                (*read_cb)(void *v);
    void                (*write_cb)(void *v);
    void                (*exit_cb)(void *v);
    void                (*del_cb)(void *v);
};

enum {
    BACKEND_SOCKET_TYPE_OUTER_SERVER,
    BACKEND_SOCKET_TYPE_INNER_SERVER,
    BACKEND_SOCKET_TYPE_DEL,
    BACKEND_SOCKET_TYPE_MAX
};

struct backend_work_thread_table {
    char                    table_name[TABLE_NAME_LEN + 1];
    pthread_mutex_t         mutex;

    struct list_table       list_head[BACKEND_SOCKET_TYPE_MAX];

    struct hash_table       hash;
    struct notify_table     notify;

    int                     event_fd;
    int                     epfd;
    struct epoll_event      *events;

    struct heap_tree        heap;
};

#define BACKEND_MAGIC               0x5a5a
#define BACKEND_RESERVE_HDR_SIZE    100
typedef enum{
    TLV_TYPE_USER_NAME       = 0,
    TLV_TYPE_PASSWORD        = 1,
    TLV_TYPE_MD5             = 2,
    TLV_TYPE_MY_DOMAIN       = 3,
    TLV_TYPE_HOST            = 4, 
}tlv_type;

enum msg_type {
    MSG_TYPE_HEART_BEAT,
    MSG_TYPE_HEART_BEAT_ACK,
    MSG_TYPE_CHALLENGE,
    MSG_TYPE_AUTH,
    MSG_TYPE_AUTH_ACK,
    MSG_TYPE_SEND_DATA,
    MSG_TYPE_MAX,
};

struct backend_hdr {
    uint16_t  magic;
    uint8_t   type;
    uint16_t  total_len;
}__attribute__((packed));

#define BACKEND_HDR_LEN (sizeof(struct backend_hdr))

struct backend_data {
    uint32_t  session_id;
}__attribute__((packed));

struct challenge_data {
    uint32_t  salt;
}__attribute__((packed));

struct auth_ack_data {
    int  status;
}__attribute__((packed));

int backend_init();
void *backend_process(void *arg);

#endif
