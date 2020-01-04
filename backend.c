#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>

#include "main.h"
#include "log.h"
#include "backend.h"
#include "buff.h"
#include "misc.h"
#include "hash_table.h"

struct backend_sk_node *backend_socket_connect_to_inner_server(uint32_t src_id);

struct backend_work_thread_table g_backend_work_thread_table;

#if 1

struct buff_table g_backend_socket_buff_table;

void backend_socket_buff_table_init()
{
    buff_table_init(&g_backend_socket_buff_table, BACKEND_SOCKET_MAX_NUM, sizeof(struct backend_sk_node), "g_backend_socket_buff_table");
}

inline struct backend_sk_node *malloc_backend_socket_node()
{
    return (struct backend_sk_node *)buff_table_malloc_node(&g_backend_socket_buff_table);
}

inline void free_backend_socket_node(struct backend_sk_node *p_node)
{
    buff_table_free_node(&g_backend_socket_buff_table, &p_node->list_head);
}

void display_g_backend_buff_table()
{
    display_buff_table(&g_backend_socket_buff_table);
}

#endif

inline void backend_add_node_to_list(struct backend_sk_node *fi)
{
    DBG_PRINTF(DBG_NORMAL, "seq_id %u:%d list add %d\n",
            fi->seq_id,
            fi->fd,
            fi->type
            );

    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;
    list_add_fe(&fi->list_head, &p_table->list_head[fi->type].list_head);
    p_table->list_head[fi->type].num++;
}

inline void backend_del_node_from_list(struct backend_sk_node *fi)
{
    DBG_PRINTF(DBG_NORMAL, "seq_id %u:%d list del %d\n",
            fi->seq_id,
            fi->fd,
            fi->type
            );

    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;
    list_del(&fi->list_head);
    p_table->list_head[fi->type].num--;
}
void backend_move_node_to_list(struct backend_sk_node *sk, int type)
{
    DBG_PRINTF(DBG_NORMAL, "seq_id %u:%d list move %d --> %d\n",
            sk->seq_id,
            sk->fd,
            sk->type,
            type);

    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;
    list_move(&sk->list_head, &p_table->list_head[type].list_head);
    if (sk->type != type) {
        p_table->list_head[sk->type].num--;
        p_table->list_head[type].num++;
        sk->type = type;
    }
}

#define BACKEND_ID_HASH(key) (*key)

DHASH_GENERATE(g_backend_work_thread_table, backend_sk_node, id_hash_node, seq_id, uint32_t, BACKEND_ID_HASH, uint32_t_cmp);

void backend_sk_raw_del(struct backend_sk_node *sk)
{
    close(sk->fd);
    if (sk->p_recv_node)
        free_notify_node(sk->p_recv_node);

    struct list_head            *p_list = NULL;
    struct list_head            *p_next = NULL;
    list_for_each_safe(p_list, p_next, &sk->send_list) {
        struct notify_node *p_entry = list_entry(p_list, struct notify_node, list_head);
        list_del(&p_entry->list_head);
        free_notify_node(p_entry);
    }

    char ip_str[32];
    uint32_t ip = htonl(sk->ip);
    DBG_PRINTF(DBG_WARNING, "raw del socket %u:%d connect from %s:%d, last_active: %d, free send node: %d\n",
            sk->seq_id,
            sk->fd,
            inet_ntop(AF_INET, &ip, ip_str, sizeof(ip_str)),
            sk->port,
            sk->last_active);

    free_backend_socket_node(sk);
}

int backend_send_data_process(struct backend_sk_node *sk)
{
    struct notify_node *p_recv_node = sk->p_recv_node;
    struct backend_hdr *p_hdr = (struct backend_hdr *)(p_recv_node->buf + p_recv_node->pos);
    struct backend_data *p_data = (struct backend_data *)(p_hdr + 1);

    uint32_t src_id = ntohl(p_data->src_id);
    uint16_t total_len = ntohs(p_hdr->total_len);
    uint16_t hdr_len = BACKEND_HDR_LEN + sizeof(struct backend_data);
    uint16_t data_len = total_len - hdr_len;
    uint8_t *p_send_data = (uint8_t *)p_hdr + hdr_len;

    struct backend_sk_node *p_node = DHASH_FIND(g_backend_work_thread_table, &g_backend_work_thread_table.hash, &src_id);
    if (p_node == NULL) {
        p_node = backend_socket_connect_to_inner_server(src_id);
    }

    if (p_node == NULL) {
        //TODO 回复close命令
        p_recv_node->end = p_recv_node->pos = 0;
    } else {
        p_node->peer            = sk;
        sk->p_recv_node = NULL;

        log_dump_hex(p_send_data, data_len);
#if 1
        p_recv_node->pos += hdr_len;
        log_dump_hex(p_recv_node->buf + p_recv_node->pos, p_recv_node->end - p_recv_node->pos);
        list_add_tail(&p_recv_node->list_head, &p_node->send_list);
        if (p_node->status == SOCKET_STATUS_CONNECTED)
            p_node->write_cb(p_node);
#endif
    }


    DBG_PRINTF(DBG_WARNING, "src_id %u data_len %hu\n",
            src_id,
            data_len);

    return 0;
}

int backend_deal_read_data_process(struct backend_sk_node *sk)
{
    struct notify_node *p_recv_node = sk->p_recv_node;
    struct backend_hdr *p_hdr = (struct backend_hdr *)(p_recv_node->buf + p_recv_node->pos);

    switch (p_hdr->type) {
    case MSG_TYPE_HEART_BEAT_ACK:
        p_recv_node->end = p_recv_node->pos = 0;
        break;

    case MSG_TYPE_SEND_DATA:
        backend_send_data_process(sk);
        break;

    default:
        sk->exit_cb(sk);
        break;
    }

    return SUCCESS;
}

int backend_inner_deal_read_data_process(struct backend_sk_node *sk)
{
    struct notify_node *p_notify_node = sk->p_recv_node;
    sk->p_recv_node = NULL;

    p_notify_node->type   = PIPE_NOTIFY_TYPE_SEND;
    p_notify_node->dst_id = 0;

    uint16_t control_len = BACKEND_HDR_LEN + sizeof(struct backend_data);
    if (control_len > p_notify_node->pos) {
        return -1;
    }

    uint16_t total_len = control_len + (p_notify_node->end - p_notify_node->pos);
    p_notify_node->pos -= control_len;
    struct backend_hdr *p_hdr = (struct backend_hdr *)(p_notify_node->buf + p_notify_node->pos);
    p_hdr->magic        = htons(BACKEND_MAGIC);
    p_hdr->type         = MSG_TYPE_SEND_DATA;
    p_hdr->total_len    = htons(total_len);

    struct backend_data *p_data = (struct backend_data *)(p_hdr + 1);
    p_data->src_id = htonl(sk->seq_id);

    struct backend_sk_node *p_server_node = sk->peer;
    list_add_tail(&p_notify_node->list_head, &p_server_node->send_list);
    p_server_node->write_cb(p_server_node);

    //log_dump_hex(p_recv_node->buf + p_recv_node->pos, p_recv_node->end - p_recv_node->pos);
    return SUCCESS;
}

void backend_socket_read_cb(void *v)
{
    struct backend_sk_node *sk = (struct backend_sk_node *)v;

    if (sk->status > SOCKET_STATUS_UNUSE_AFTER_SEND)
        return;

    sk->last_active = time(NULL);
    while(1) {
        struct notify_node *p_recv_node = sk->p_recv_node;
        if (p_recv_node == NULL) {
            p_recv_node = malloc_notify_node();
            if (p_recv_node == NULL) {
                DBG_PRINTF(DBG_WARNING, "socket %u:%d, no avaiable space, drop data!\n",
                        sk->seq_id,
                        sk->fd);
                break;
            } else {
                p_recv_node->pos = 0;
                p_recv_node->end = 0;
                sk->p_recv_node = p_recv_node;
            }
        }

        uint16_t n_recv = p_recv_node->end - p_recv_node->pos;
        int to_recv;

        if (n_recv < BACKEND_HDR_LEN) {
            to_recv = BACKEND_HDR_LEN - n_recv;
        } else {
            struct backend_hdr *p_hdr = (struct backend_hdr *)(p_recv_node->buf + p_recv_node->pos);
            if (p_hdr->magic != htons(BACKEND_MAGIC)) {
                DBG_PRINTF(DBG_ERROR, "socket %u:%d, magic error: %hu\n",
                        sk->seq_id,
                        sk->fd,
                        htons(p_hdr->magic));

                p_recv_node->end = 0;
                sk->exit_cb((void *)sk);
                break;
            }

            uint16_t total_len = ntohs(p_hdr->total_len);
            if ((total_len > (MAX_BUFF_SIZE - p_recv_node->pos))
                    || (total_len < n_recv)) {
                DBG_PRINTF(DBG_ERROR, "socket %u:%d, critical nrecv: %hu, total_len: %hu, pos: %hu, end: %hu\n",
                        sk->seq_id,
                        sk->fd,
                        n_recv,
                        total_len,
                        p_recv_node->pos,
                        p_recv_node->end);
                log_dump_hex((const uint8_t *)p_hdr, n_recv);
                sk->exit_cb((void *)sk);
                break;
            }

            if (n_recv == total_len) {
                log_dump_hex((const uint8_t *)p_recv_node->buf + p_recv_node->pos, p_recv_node->end - p_recv_node->pos);
                backend_deal_read_data_process(sk);
                continue;
            }

            to_recv = total_len - n_recv;
        }

        int nread = recv(sk->fd, p_recv_node->buf + p_recv_node->end, to_recv, MSG_DONTWAIT);
        if (nread > 0) {
            p_recv_node->end += nread;
            continue;
        }

        if (nread == 0) {
            DBG_PRINTF(DBG_NORMAL, "socket %u:%d closed by peer\n",
                    sk->seq_id,
                    sk->fd);
            sk->exit_cb((void *)sk);
            break;
        }

        if (errno == EAGAIN) {
            DBG_PRINTF(DBG_NORMAL, "socket %u:%d need recv next!\n",
                    sk->seq_id,
                    sk->fd);
            break;
        } else if (errno == EINTR) {
            DBG_PRINTF(DBG_ERROR, "socket %u:%d need recv again!\n",
                    sk->seq_id,
                    sk->fd);
            continue;
        } else {
            DBG_PRINTF(DBG_NORMAL, "socket %u:%d errno: %d\n",
                    sk->seq_id,
                    sk->fd,
                    errno);
            sk->exit_cb((void *)sk);
            break;
        }
    }
}

void backend_inner_socket_read_cb(void *v)
{
    struct backend_sk_node *sk = (struct backend_sk_node *)v;

    if (sk->status > SOCKET_STATUS_UNUSE_AFTER_SEND)
        return;

    sk->last_active = time(NULL);
    while(1) {
        struct notify_node *p_recv_node = sk->p_recv_node;
        if (p_recv_node == NULL) {
            p_recv_node = malloc_notify_node();
            if (p_recv_node == NULL) {
                DBG_PRINTF(DBG_WARNING, "socket %u:%d, no avaiable space, drop data!\n",
                        sk->seq_id,
                        sk->fd);
                break;
            } else {
                p_recv_node->pos = p_recv_node->end = BACKEND_RESERVE_HDR_SIZE;
                sk->p_recv_node = p_recv_node;
            }
        }

        uint16_t n_recv = p_recv_node->end - p_recv_node->pos;
        uint16_t to_recv = MAX_BUFF_SIZE - n_recv;

        int nread = recv(sk->fd, p_recv_node->buf + p_recv_node->end, to_recv, MSG_DONTWAIT);
        if (nread > 0) {
            p_recv_node->end += nread;
            backend_inner_deal_read_data_process(sk);
            continue;
        }

        if (nread == 0) {
            DBG_PRINTF(DBG_NORMAL, "socket %u:%d closed by peer\n",
                    sk->seq_id,
                    sk->fd);
            sk->exit_cb((void *)sk);
            break;
        }

        if (errno == EAGAIN) {
            DBG_PRINTF(DBG_NORMAL, "socket %u:%d need recv next!\n",
                    sk->seq_id,
                    sk->fd);
            break;
        } else if (errno == EINTR) {
            DBG_PRINTF(DBG_ERROR, "socket %u:%d need recv again!\n",
                    sk->seq_id,
                    sk->fd);
            continue;
        } else {
            DBG_PRINTF(DBG_NORMAL, "socket %u:%d errno: %d\n",
                    sk->seq_id,
                    sk->fd,
                    errno);
            sk->exit_cb((void *)sk);
            break;
        }
    }
}

void backend_socket_write_cb(void *v)
{
    struct backend_sk_node *sk = (struct backend_sk_node *)v;
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;

    int fd = sk->fd;
    uint32_t seq_id = sk->seq_id;

    if (sk->status > SOCKET_STATUS_UNUSE_AFTER_SEND) {
        DBG_PRINTF(DBG_WARNING, "seq_id %u:%d status %d!\n",
                seq_id,
                fd,
                sk->status);
        return;
    }

    if (sk->blocked)
        return;

    struct list_head            *p_list = NULL;
    struct list_head            *p_next = NULL;
    list_for_each_safe(p_list, p_next, &sk->send_list) {
        struct notify_node *p_entry = list_entry(p_list, struct notify_node, list_head);

        DBG_PRINTF(DBG_NORMAL, "seq_id %u:%d, src_id: %u, send buf pos %hu, end %hu\n",
                seq_id,
                fd,
                p_entry->type,
                p_entry->src_id,
                p_entry->pos,
                p_entry->end);

        int nwrite = 0;
        int to_write = p_entry->end - p_entry->pos;
        do {
            p_entry->pos = p_entry->pos + nwrite;
            to_write = p_entry->end - p_entry->pos;
            if (to_write == 0)
                break;

            nwrite = send(fd, p_entry->buf + p_entry->pos, to_write, 0);

            if (g_main_debug >= DBG_NORMAL) {
                log_dump_hex(p_entry->buf + p_entry->pos, to_write);
                DBG_PRINTF(DBG_CLOSE, "seq_id %u:%d nwrite: %d, front_listen_id %u\n",
                        seq_id,
                        fd,
                        nwrite);
            }
        } while(nwrite > 0);

        if (to_write == 0) {
            DBG_PRINTF(DBG_NORMAL, "seq_id %u:%d no data to write!\n",
                    seq_id,
                    fd);

            list_del(&p_entry->list_head);
            free_notify_node(p_entry);
            continue;
        }

        if (nwrite < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                DBG_PRINTF(DBG_WARNING, "seq_id %u:%d cannot write!\n",
                        seq_id,
                        fd);
                modify_event(p_table->epfd, fd, (void *)sk, EPOLLIN | EPOLLOUT);// | EPOLLET);
                sk->blocked = 1;
                goto WRITE_EXIT;
            } else {
                DBG_PRINTF(DBG_ERROR, "seq_id %u:%d errno: %d, error msg: %s!\n",
                        seq_id,
                        fd,
                        errno,
                        strerror(errno));
                sk->exit_cb((void *)sk);
                return;
            }
        } else {
            DBG_PRINTF(DBG_ERROR, "critical seq_id %u:%d, nwrite: %d, to_write: %d\n",
                    seq_id,
                    fd,
                    nwrite,
                    to_write);
            sk->exit_cb((void *)sk);
            return;
        }
    }

    if (sk->status == SOCKET_STATUS_UNUSE_AFTER_SEND) {
        sk->exit_cb((void *)sk);
    } else {
        modify_event(p_table->epfd, fd, (void *)sk, EPOLLIN);// | EPOLLET);
    }

WRITE_EXIT:
    return;
}

void backend_socket_connect_cb(void *v)
{
    struct backend_sk_node *fi = (struct backend_sk_node *)v;
    int fd = fi->fd;
    int result;
    socklen_t result_len = sizeof(result);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
        DBG_PRINTF(DBG_ERROR, "%u, connect failed\n",
                fd);
        return;
    }
    if (result != 0) {
        DBG_PRINTF(DBG_ERROR, "%u, connect failed\n",
                fd);
        return;
    }

    DBG_PRINTF(DBG_CLOSE, "%u, connect success\n",
            fd);

    fi->status   = SOCKET_STATUS_CONNECTED;
    fi->write_cb = backend_socket_write_cb;
    //TODO 添加timer，以便定时发送信息
}

void backend_socket_exit_cb(void *v)
{
    struct backend_sk_node *sk = (struct backend_sk_node *)v;
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;

    if (sk->status == SOCKET_STATUS_DEL) {
        DBG_PRINTF(DBG_ERROR, "seq_id %u:%d critical error alread del\n",
                sk->seq_id,
                sk->fd);
        return;
    }

    delete_event(p_table->epfd, sk->fd, sk, EPOLLIN | EPOLLOUT);

    close(sk->fd);
    sk->status = SOCKET_STATUS_DEL;

    if (sk->id_hash_node.prev != NULL) {
        list_del(&sk->id_hash_node);
        sk->id_hash_node.prev = sk->id_hash_node.next = NULL;
    }

    backend_move_node_to_list(sk, BACKEND_SOCKET_TYPE_DEL);
    if (g_main_debug >= DBG_NORMAL) {
        char ip_str[30];
        uint32_t ip = htonl(sk->ip);
        DBG_PRINTF(DBG_WARNING, "exit seq_id %u:%d connect from %s:%d, alive_cnt: %u, ttl: %d\n",
                sk->seq_id,
                sk->fd,
                inet_ntop(AF_INET, &ip, ip_str, sizeof(ip_str)),
                sk->port,
                sk->alive_cnt,
                time(NULL) - sk->last_active);
    }
}

void backend_socket_del_cb(void *v)
{
    struct backend_sk_node *sk = (struct backend_sk_node *)v;
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;

    if (sk->type != BACKEND_SOCKET_TYPE_DEL) {
        DBG_PRINTF(DBG_ERROR, "user %u critical error %u:%d last_active: %d type: %hhu status: %hhu\n",
                sk->seq_id,
                sk->fd,
                sk->last_active,
                sk->type,
                sk->status);
    }

    struct list_table *p_list_table = &p_table->list_head[sk->type];

    list_del(&sk->list_head);
    p_list_table->num--;

    if (sk->p_recv_node) {
        free_notify_node(sk->p_recv_node);
        sk->p_recv_node = NULL;
    }

    struct list_head            *p_list = NULL;
    struct list_head            *p_next = NULL;
    list_for_each_safe(p_list, p_next, &sk->send_list) {
        struct notify_node *p_entry = list_entry(p_list, struct notify_node, list_head);
        list_del(&p_entry->list_head);
        free_notify_node(p_entry);
    }

    DBG_PRINTF(DBG_NORMAL, "user %u del socket %u:%d free send node: %d\n",
            sk->seq_id,
            sk->fd);

    free_backend_socket_node(sk);
}

/*
 * 创建到真实服务器的连接
 */
struct backend_sk_node *backend_socket_connect_to_inner_server(uint32_t src_id)
{
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;

    int new_socket = 0;
    uint32_t inner_ip = get_ip_by_hostname(INNER_HOST);
    if (inner_ip == 0) {
        return NULL;
    }
    //TODO forbidden circle connect
#if 0
    if (inner_ip == server_ip) {
        return NULL;
    }
#endif
    int ret = create_socket_to_server(inner_ip, INNER_PORT, 0, &new_socket);
    if (ret == -1) {
        DBG_PRINTF(DBG_ERROR, "create connect socket failed at %s:%d, errnum: %d\n",
                INNER_HOST,
                INNER_PORT,
                ret);
        return NULL;
    }

    struct backend_sk_node *p_node = malloc_backend_socket_node();
    if (p_node == NULL) {
        DBG_PRINTF(DBG_ERROR, "malloc socket node failed\n");
        close(new_socket);
        return NULL;
    }

    time_t now = time(NULL);
    p_node->fd              = new_socket;
    p_node->seq_id          = src_id;
    p_node->ip              = inner_ip;
    p_node->port            = INNER_PORT;
    p_node->p_recv_node     = NULL;
    p_node->last_active     = now;
    p_node->last_hb_time    = 0;
    p_node->type            = BACKEND_SOCKET_TYPE_INNER_SERVER;
    p_node->blocked         = 0;

    p_node->read_cb         = backend_inner_socket_read_cb;
    if (ret == 0) {
        p_node->write_cb    = backend_socket_write_cb;
        p_node->status      = SOCKET_STATUS_CONNECTED;
    } else {
        p_node->write_cb    = backend_socket_connect_cb;
        p_node->status      = SOCKET_STATUS_CONNECTING;
    }
    p_node->exit_cb         = backend_socket_exit_cb;
    p_node->del_cb          = backend_socket_del_cb;

    INIT_LIST_HEAD(&p_node->send_list);

    if (-1 == DHASH_INSERT(g_backend_work_thread_table, &p_table->hash, p_node)) {
        DBG_PRINTF(DBG_ERROR, "new socket %u:%d exist!\n",
                p_node->seq_id,
                p_node->fd);
        close(new_socket);
        free_backend_socket_node(p_node);
        return NULL;
    }

    backend_add_node_to_list(p_node);

    set_none_block(p_node->fd);
    add_event(p_table->epfd, p_node->fd, p_node, EPOLLIN | EPOLLOUT | EPOLLERR);

    DBG_PRINTF(DBG_ERROR, "create connect socket success to %s:%d, socket:%d, errnum: %d\n",
            INNER_HOST,
            INNER_PORT,
            p_node->fd,
            ret);

    return p_node;
}

/*
 * 创建到转发服务器的连接
 * 返回值：
 * 0 成功
 * -1 失败
 */
int backend_socket_connect_to_server()
{
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;

    int new_socket = 0;
    int ret = create_socket_to_server_by_host(BACKEND_HOST, BACKEND_PORT, 0, &new_socket);
    if (ret == -1) {
        DBG_PRINTF(DBG_ERROR, "create connect socket failed at %s:%d, errnum: %d\n",
                BACKEND_HOST,
                BACKEND_PORT,
                ret);
        return -1;
    }

    struct backend_sk_node *p_node = malloc_backend_socket_node();
    if (p_node == NULL) {
        DBG_PRINTF(DBG_ERROR, "malloc socket node failed\n");
        close(new_socket);
        return -1;
    }

    time_t now = time(NULL);
    p_node->fd              = new_socket;
    p_node->p_recv_node     = NULL;
    p_node->last_active     = now;
    p_node->last_hb_time    = 0;
    p_node->type            = BACKEND_SOCKET_TYPE_OUTER_SERVER;
    p_node->blocked         = 0;

    p_node->read_cb         = backend_socket_read_cb;
    if (ret == 0) {
        p_node->write_cb    = backend_socket_write_cb;
        p_node->status      = SOCKET_STATUS_CONNECTED;
    } else {
        p_node->write_cb    = backend_socket_connect_cb;
        p_node->status      = SOCKET_STATUS_CONNECTING;
    }
    p_node->exit_cb         = backend_socket_exit_cb;
    p_node->del_cb          = backend_socket_del_cb;

    INIT_LIST_HEAD(&p_node->send_list);

    backend_add_node_to_list(p_node);

    set_none_block(p_node->fd);
    add_event(p_table->epfd, p_node->fd, p_node, EPOLLIN | EPOLLOUT | EPOLLERR);

    DBG_PRINTF(DBG_ERROR, "create connect socket success to %s:%d, socket:%d, errnum: %d\n",
            BACKEND_HOST,
            BACKEND_PORT,
            p_node->fd,
            ret);

    return 0;
}


int backend_send_heart_beat(struct backend_sk_node *sk)
{
    sk->last_hb_time = time(NULL);

    DBG_PRINTF(DBG_WARNING, "sk %d, send heart beat\n",
            sk->seq_id);
    
    struct notify_node *p_notify_node = malloc_notify_node();
    if (p_notify_node == NULL)
        return FAIL;

    p_notify_node->type = PIPE_NOTIFY_TYPE_SEND;
    p_notify_node->pos  = 0;

    uint16_t                total_len = sizeof(struct backend_hdr);
    struct backend_hdr      *p_hdr   = (struct backend_hdr *)p_notify_node->buf;

    p_hdr->magic        = htons(BACKEND_MAGIC);
    p_hdr->type         = MSG_TYPE_HEART_BEAT;
    p_hdr->total_len    = htons(total_len);
    p_notify_node->end  = total_len;
    
    list_add_tail(&p_notify_node->list_head, &sk->send_list);
    sk->write_cb((void *)sk);
    return 0;
}

void backend_heart_beat_process(struct list_table *p_list_table, char *table_name)
{
    int                         count = 0;
    time_t                      now = time(NULL);
    time_t                      hb_time = now - 30;
    uint32_t                    total_num = p_list_table->num;
    struct list_head            *p_list = NULL;

    list_for_each(p_list, &p_list_table->list_head) {
        struct backend_sk_node *p_entry = list_entry(p_list, struct backend_sk_node, list_head);
        if (p_entry->status == SOCKET_STATUS_CONNECTED) {
            if (p_entry->last_hb_time < hb_time) {
                backend_send_heart_beat(p_entry);
            }
        } else {
	        DBG_PRINTF(DBG_WARNING, "critical error at: %s %u:%d status %d\n",
                table_name,
                p_entry->seq_id,
                p_entry->fd,
                p_entry->status);
        }
    }

    DBG_PRINTF(DBG_WARNING, "%s list table: %d total_num: %u, old: %d, delay: %d\n", table_name, total_num, count, time(NULL) - now);
}

void *backend_process(void *arg)
{
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;
    time_t last_time = time(NULL);

    prctl(PR_SET_NAME, p_table->table_name);

    DBG_PRINTF(DBG_WARNING, "%s enter timerstamp %d\n", p_table->table_name, last_time);

    while(g_main_running) {
        int nfds = epoll_wait(p_table->epfd, p_table->events, BACKEND_THREAD_EPOLL_MAX_EVENTS, 1 * 1000);

        int i;
        for( i= 0; i < nfds; ++i) {
            struct backend_sk_node *sk = (struct backend_sk_node *)(p_table->events[i].data.ptr);

            if(p_table->events[i].events & EPOLLIN) {
                sk->read_cb(sk);
            } else if(p_table->events[i].events & EPOLLOUT) {
                sk->blocked = 0;
                sk->write_cb(sk);
            } else {
                DBG_PRINTF(DBG_ERROR, "%u:%d, type:%d unknown event: %d\n",
                        sk->seq_id,
                        sk->fd,
                        sk->type,
                        p_table->events[i].events);
            }
        }

        time_t now = time(NULL);
        if ((now - last_time) > 10) {
            last_time = now;
            backend_heart_beat_process(&p_table->list_head[BACKEND_SOCKET_TYPE_OUTER_SERVER], p_table->table_name);
        }

        //manage_del_process(&p_table->list_head[MANAGE_UNUSE_SOCKET_TYPE_DEL], p_table->table_name);
    }

    DBG_PRINTF(DBG_WARNING, "leave timestamp %d\n", time(NULL));

    exit(EXIT_SUCCESS);
}

int backend_init()
{
    struct backend_work_thread_table *p_table = &g_backend_work_thread_table;

    p_table->events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * BACKEND_ACCEPT_EPOLL_MAX_EVENTS);
    if (p_table->events == NULL)
        exit(EXIT_FAILURE);
    p_table->epfd = epoll_create(BACKEND_ACCEPT_EPOLL_MAX_EVENTS);

    pthread_mutex_init(&p_table->mutex, NULL);
    sprintf(p_table->table_name, "backend_work");

    int j;
    for (j = 0; j < BACKEND_SOCKET_TYPE_MAX; j++) {
        INIT_LIST_HEAD(&p_table->list_head[j].list_head);
        p_table->list_head[j].num = 0;
    }
    DHASH_INIT(g_backend_work_thread_table, &g_backend_work_thread_table.hash, BACKEND_THREAD_HASH_SIZE);

    backend_socket_buff_table_init();

    backend_socket_connect_to_server();
    return 0;
}

inline void backend_event_notify(int event_fd)
{
    uint64_t notify = 1;
    if (write(event_fd, &notify, sizeof(notify)) < 0) {
        DBG_PRINTF(DBG_WARNING, "event_fd %d, write error!\n",
                event_fd);
    }
}

#if 0
void backend_socket_handle_accpet_cb()
{
    struct accept_socket_table *p_table = (struct accept_socket_table *)&g_backend_accept_socket_table;
    struct sockaddr_in  client_addr;
    socklen_t           length          = sizeof(client_addr);
    int                 new_socket      = accept(p_table->fd, (struct sockaddr*)&client_addr, &length);

    if (new_socket < 0) {
        DBG_PRINTF(DBG_ERROR, "Accept Failed! error no: %d, error msg: %s\n",
                errno,
                strerror(errno));
        return;
    }

    struct backend_sk_node *p_node = malloc_backend_socket_node();
    if (p_node == NULL) {
        char ip_str[32];
        DBG_PRINTF(DBG_ERROR, "new socket %d connect from %s:%hu failed\n",
                new_socket,
                inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ip_str, sizeof(ip_str)),
                client_addr.sin_port);
        close(new_socket);
        return;
    }

    uint32_t ip = ntohl(client_addr.sin_addr.s_addr);

    p_node->mac_hash_node.prev = p_node->mac_hash_node.next = NULL;
    p_node->id_hash_node.prev = p_node->id_hash_node.next = NULL;
    p_node->fd              = new_socket;
    p_node->ip              = ip;
    p_node->port            = ntohs(client_addr.sin_port);
    p_node->p_recv_node     = NULL;
    p_node->last_active     = time(NULL);
    p_node->alive_cnt       = 0;
    p_node->quality         = 0;
    p_node->blocked         = 0;

    INIT_LIST_HEAD(&p_node->send_list);

    if (backend_notify_new_socket(p_node) == -1) {
        close(new_socket);
        free_backend_socket_node(p_node);

        char ip_str[32];
        DBG_PRINTF(DBG_CLOSE, "new socket %d seq_id %u connect from %s:%hu failed\n",
                new_socket,
                p_node->seq_id,
                inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ip_str, sizeof(ip_str)),
                client_addr.sin_port);
        return;
    } else {
        char ip_str[32];
        DBG_PRINTF(DBG_NORMAL, "new socket %d seq_id %u connect from %s:%hu success\n",
                new_socket,
                p_node->seq_id,
                inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ip_str, sizeof(ip_str)),
                client_addr.sin_port);
    }
}
#endif
