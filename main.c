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
#include <sys/prctl.h>

#include "main.h"
#include "log.h"
#include "backend.h"
#include "unique_id.h"

int g_main_running = 1;
int g_main_debug = DBG_NORMAL;

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

    unique_id_init();
    notify_buf_table_init();

    backend_init();

    return 0;
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
    bool daemon = false;
    char *config = "main.conf";

    int c, option_index;
    static struct option long_options[] = {
        {"daemon",  no_argument,        NULL,   'd'},
        {"config",  required_argument,  NULL,   'c'},
        {"version", no_argument,        NULL,   'v'},
        {"server",  required_argument,  NULL,   's'},
        {NULL,      0,                  NULL,   0}
    };

    while (-1 != (c = getopt_long(argc, argv, "dc:v:s", long_options, &option_index))) {
        switch (c) {
        case 'd':
            daemon = true;
            break;

        case 'c':
            config = optarg;
            (void)config;
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

#if 0
    if (-1 == load_config_from_file(config, my_index, my_name)) {
        exit(EXIT_FAILURE);
    }
#endif

    log_init("/var/log/crossnet_client.log");

    if (daemon) {
        main_daemon();
    }

    //display_g_main_config();

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
