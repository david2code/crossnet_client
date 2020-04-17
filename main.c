//#define _GNU_SOURCE
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
#include <json-c/json.h>
#include <sys/prctl.h>

#include "main.h"
#include "log.h"
#include "backend.h"
#include "unique_id.h"
#include "config.h"

int g_main_running = 1;
int g_main_debug = DBG_NORMAL;
struct ctx g_ctx;

void *timer_process(void *arg)
{
    prctl(PR_SET_NAME, __FUNCTION__);

    DBG_PRINTF(DBG_WARNING, "enter timerstamp %d\n", time(NULL));

    while(g_main_running) {
        sleep(20);
    }

    DBG_PRINTF(DBG_WARNING, "leave timestamp %d\n", time(NULL));

    exit(EXIT_SUCCESS);
}

int main_daemon()
{
    switch (fork()) {
    case -1:
        DBG_PRINTF(DBG_ERROR, "fork() failed");
        return -1;

    case 0:
        break;

    default:
        exit(0);
    }

    if (setsid() == -1) {
        DBG_PRINTF(DBG_ERROR, "setsid() failed");
        return -1;
    }

    umask(0);

    int fd = open("/dev/null", O_RDWR);
    if (fd == -1) {
        DBG_PRINTF(DBG_ERROR, "open(\"/dev/null\") failed");
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) == -1) {
        DBG_PRINTF(DBG_ERROR, "dup2(STDIN) failed");
        return -1;
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        DBG_PRINTF(DBG_ERROR, "dup2(STDOUT) failed");
        return -1;
    }

    if (fd > STDERR_FILENO) {
        if (close(fd) == -1) {
            DBG_PRINTF(DBG_ERROR, "close() failed");
            return -1;
        }
    }

    return 0;
}

/* Signale wrapper. */
void signal_set(int signo, void (*func)(int))
{
    struct sigaction sig;
    struct sigaction osig;

    sig.sa_handler = func;
    sig.sa_flags = 0;
#ifdef SA_RESTART
    sig.sa_flags |= SA_RESTART;
#endif /* SA_RESTART */

    if ((-1 == sigemptyset (&sig.sa_mask))
            || (-1 == sigaction (signo, &sig, &osig))) {
        DBG_PRINTF(DBG_CLOSE, "signal %d, error\n", signo);
        perror("failed to set signal\n");
        exit(EXIT_FAILURE);
    }
}

void sigfun(int sig)
{
    DBG_PRINTF(DBG_CLOSE, "signal %d\n", sig);
    signal_set(sig, SIG_DFL);
    g_main_running = 0;
}

void signal_init ()
{
    //signal_set (SIGINT, sigfun);
    signal_set (SIGTSTP, sigfun);
    //signal_set (SIGKILL, sigfun);
    //signal_set (SIGTERM, sigfun);
    signal_set (SIGSEGV, sigfun);
}

int init()
{
    signal_init();

    //unique_id_init();
    notify_buf_table_init();

    backend_init();

    return 0;
}

int parse_json_config(char *config_str)
{
    int ret = SUCCESS;
    struct ctx *p_ctx = &g_ctx;

    json_object *obj = json_tokener_parse(config_str);
    if (!obj) {   
        printf("json_parse_error\n");
        ret = FAIL;
        goto JSON_ERROR_END;
    }

    json_type type = json_object_get_type(obj);
    if(type != json_type_object) {   
        printf("json_type: %d\n", type);
        ret = FAIL;
        goto JSON_ERROR_END;
    }

    json_object_object_foreach(obj, key, val) {   
        json_type type =json_object_get_type(val);

        if (strcmp(key, "server_host") == 0) {   
            if(type == json_type_string) {
                snprintf(p_ctx->server_host, HOST_MAX_LEN + 1, "%s", json_object_get_string(val));
                p_ctx->server_host[HOST_MAX_LEN] = 0;
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else if (strcmp(key, "server_port") == 0) {   
            if(type == json_type_int) {
                p_ctx->server_port = json_object_get_int(val);
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else if (strcmp(key, "my_host") == 0) {   
            if(type == json_type_string) {
                snprintf(p_ctx->my_host, HOST_MAX_LEN + 1, "%s", json_object_get_string(val));
                p_ctx->my_host[HOST_MAX_LEN] = 0;
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else if (strcmp(key, "my_port") == 0) {   
            if(type == json_type_int) {
                p_ctx->my_port = json_object_get_int(val);
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else if (strcmp(key, "user_name") == 0) {   
            if(type == json_type_string) {
                snprintf(p_ctx->user_name, USER_NAME_MAX_LEN + 1, "%s", json_object_get_string(val));
                p_ctx->user_name[USER_NAME_MAX_LEN] = 0;
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else if (strcmp(key, "password") == 0) {   
            if(type == json_type_string) {
                snprintf(p_ctx->password, PASSWORD_MAX_LEN + 1, "%s", json_object_get_string(val));
                p_ctx->password[PASSWORD_MAX_LEN] = 0;
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else if (strcmp(key, "log_file") == 0) {   
            if(type == json_type_string) {
                snprintf(p_ctx->log_file, LOG_FILE_NAME_MAX_LEN + 1, "%s", json_object_get_string(val));
                p_ctx->log_file[LOG_FILE_NAME_MAX_LEN] = 0;
            } else {
                ret = FAIL;
                goto JSON_ERROR_END;
            }
        } else {
            printf("unknown json key: %s\n", key);
        }
    }

JSON_ERROR_END:
    json_object_put(obj);

    return ret;
}

int load_config_from_json_file(char *config)
{
    FILE  *fp        = NULL;
    char buffer[2048] = {0};
    int ret = 0;

    if (config == NULL) {
        printf("config file not found!\n");
        return FAIL;
    }

    if (NULL == (fp = fopen(config, "r"))) {
        printf("open %s failed!\n", config);
        return FAIL;
    }

    ret = fread(buffer, 1, 2048, fp);
    if (ret <= 0) {
        printf("config file %s empty!\n", config);
        return FAIL;
    }
    if (ret >= 2048) {
        printf("config file %s too big!\n", config);
        return FAIL;
    }

    //printf("%d\n", ret);
    //printf("%s\n", buffer);
    return parse_json_config(buffer);
}

int check_and_print_ctx()
{
    struct ctx *p_ctx = &g_ctx;

    if (!p_ctx->server_host[0]) {
        printf("server_host should not be empty!\n");
        return FAIL;
    }
    if (p_ctx->server_port < 1) {
        printf("server_port should not be zero!\n");
        return FAIL;
    }
    if (!p_ctx->user_name[0]) {
        printf("user_name should not be empty!\n");
        return FAIL;
    }
    if (!p_ctx->password[0]) {
        printf("password should not be empty!\n");
        return FAIL;
    }
    if (!p_ctx->my_host[0]) {
        printf("my_host should not be empty!\n");
        return FAIL;
    }
    if (p_ctx->my_port < 1) {
        printf("my_port should not be zero!\n");
        return FAIL;
    }

    printf("config success!\n");
    printf("server: %s\n", p_ctx->server_host);
    printf("port: %hu\n", p_ctx->server_port);
    printf("user_name: %s\n", p_ctx->user_name);
    printf("password: %s\n", p_ctx->password);
    printf("my_host: %s\n", p_ctx->my_host);
    printf("my_port: %hu\n", p_ctx->my_port);
    printf("log_file: %s\n", p_ctx->log_file);
    printf("primary_ver: %hhu\n", p_ctx->primary_ver);
    printf("secondary_ver: %hhu\n", p_ctx->secondary_ver);

    return SUCCESS;
}

/*
 * 启动后根据配置,连接服务器
 * 认证信息
 * 收到服务器发来的数据后,创建通往内网服务器的连接并将数据交付
 * 收到服务器发来的关闭连接命令,关闭相应的连接
 * windows环境下,用户可以关闭线程.修改完配置后,重新生成线程
 */
int main(int argc, char **argv)
{
    bool daemon = true;
    char *config = "config.json";
    int len;

    int c, option_index;
    static struct option long_options[] = {
        {"config",  required_argument,  NULL,   'c'},
        {"version", no_argument,        NULL,   'v'},
        {NULL,      0,                  NULL,   0}
    };

    memset(&g_ctx, 0, sizeof(struct ctx));
    strncpy(g_ctx.log_file, "crossnet_client.log", LOG_FILE_NAME_MAX_LEN);
    sscanf(VERSION, "%hhu.%hhu", &g_ctx.primary_ver, &g_ctx.secondary_ver);
    while (-1 != (c = getopt_long(argc, argv, "c:v", long_options, &option_index))) {
        switch (c) {

        case 'c':
            config = optarg;
            //printf("config %s\n", config);
            break;

        case 'v':
            printf("%s\n", VERSION);
            exit(0);
            break;

        default:
            printf("?? getopt returned character code 0%o ??\n", c);
            exit(EXIT_FAILURE);
        }
    }

    if (FAIL == load_config_from_json_file(config)) {
        exit(EXIT_FAILURE);
    }

    if (FAIL == check_and_print_ctx()) {
        exit(EXIT_FAILURE);
    }

    log_init(g_ctx.log_file);

    if (daemon) {
        main_daemon();
    }


    init();

#if 0
    setvbuf(stdout,NULL,_IONBF,0);
#endif

    int res;
    pthread_t backend_thread;
    pthread_t timer_thread;

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);
    int rc = pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);
    if (rc != 0) {
        printf("block sigpipe error\n");
    }

    res = pthread_create(&backend_thread, NULL, backend_process, NULL);
    if (res != 0) {
        perror("Thread creation failed!");
        exit(EXIT_FAILURE);
    }

    res = pthread_create(&timer_thread, NULL, timer_process, NULL);
    if (res != 0) {
        perror("Thread creation failed!");
        exit(EXIT_FAILURE);
    }

    res = pthread_join(backend_thread, NULL);
    if (res != 0) {
        perror("Thread join failed!");
        exit(EXIT_FAILURE);
    }

    res = pthread_join(timer_thread, NULL);
    if (res != 0) {
        perror("Thread join failed!");
        exit(EXIT_FAILURE);
    }

    return 0;
}
