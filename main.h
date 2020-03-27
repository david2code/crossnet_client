#ifndef _MAIN_H
#define _MAIN_H

#include "misc.h"
#include "kernel_list.h"

#define USER_NUM      100

#define BACKEND_HOST   "www.code.com"
#define BACKEND_PORT   66
#define BACKEND_WORK_THREAD_NUM    4

#if 0
#define INNER_HOST   "www.david.com"
#define INNER_PORT   8080 
#else
#define INNER_HOST   "xqctk.jtys.sz.gov.cn"
#define INNER_PORT   80
#endif

#define TABLE_NAME_LEN  50
#define MAX_BUFF_SIZE            2048

#define SUCCESS    0
#define FAIL       -1
#define NEED_MORE  -2

#define HOST_MAX_LEN            100
#define USER_NAME_MAX_LEN       100
#define PASSWORD_MAX_LEN        100
#define LOG_FILE_NAME_MAX_LEN   100

struct ctx {
    char        server_ip[HOST_MAX_LEN + 1];
    uint16_t    server_port;

    char        user_name[USER_NAME_MAX_LEN + 1];
    char        password[PASSWORD_MAX_LEN + 1];

    char        my_ip[HOST_MAX_LEN + 1];
    uint16_t    my_port;

    char        log_file[LOG_FILE_NAME_MAX_LEN + 1];
};

struct list_table {
    struct list_head    list_head;
    uint32_t            num;
};


struct accept_socket_table {
    int                     fd;
    int                     event_fd;
    int                     epfd;
    struct epoll_event      *events;
};

extern int g_main_running;
#endif
