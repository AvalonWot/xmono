/**
 * Author:      skeu (skeu.grass@gmail.com)
 * DateTime:    2014/07/10
 * Description: lightweight per to per network module
 */

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ecmd.h"

// #define LOGD(fmt, args...)  printf(fmt, ##args)
// #define LOGE(fmt, args...)  printf(fmt, ##args)
#include <android/log.h>
#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)


struct _RespCallback {
    RespCallback *next;
    uint32_t cmd_id;
    RespCallbackFunc func;
};

#ifdef _WIN32
#define THREADCALL WINAPI
#define close(x) closesocket(x)
#else
#define THREADCALL
#define SOCKET int
#endif

/*全局变量定义*/
static ErrCallbackFunc err_callback;
static int (*restart) () = 0;
static SOCKET _socket_fd = 0; /*全局使用的通信socket*/
static RespCallback *resp_callback_list = 0;
static void *callbask_list_mutex = 0;
static char _ip[32];
static uint16_t _port = 0;

/*平台系统api*/
static int create_thread (void *func, void *args) {
#ifdef _WIN32
    HANDLE hThread = CreateThread (0, 0, (LPTHREAD_START_ROUTINE)func, args, 0, 0);
    if (!hThread) return 0;
    CloseHandle (hThread);
    return 1;
#else
    pthread_t tid;
    if (pthread_create(&tid, NULL, func, 0))
        return 0;
    return 1;
#endif
}

static void create_mutex (void **mutex) {
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)malloc (sizeof (CRITICAL_SECTION));
    InitializeCriticalSection (cs);
    *mutex = cs;
#else
    pthread_mutex_t *pmt = (pthread_mutex_t*)malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init (pmt, 0);
    *mutex = pmt;
#endif
}

static void mutex_lock (void *mutex) {
#ifdef _WIN32
    EnterCriticalSection ((CRITICAL_SECTION*)mutex);
#else
    pthread_mutex_lock ((pthread_mutex_t*)mutex);
#endif
}

static void mutex_unlock (void *mutex) {
#ifdef _WIN32
    LeaveCriticalSection ((CRITICAL_SECTION*)mutex);
#else
    pthread_mutex_unlock ((pthread_mutex_t*)mutex);
#endif
}


/*调试帮助函数*/
static void dump_resp_callback_list () {
    RespCallback *list;
    for (list = resp_callback_list->next; list; list = list->next) {
        printf("resp_callback_list dump:\n");
        printf("\t cmd_id : %d\n", list->cmd_id);
        printf("\t func : %p\n", list->func);
    }
}

static char const *err_str = "-----";
char const *ecmd_err_str () {
    return err_str;
}

static void set_error_str (char const *fmt, ...) {
    // TODO : 格式化错误结果
    err_str = fmt;
    return;
}

static void clear_err_str () {
}

static int send_length (int fd, const char *buf, int len) {
    int res;
    int total = 0;

    if (len == 0) return 0;

    do {
        res = send (fd, buf + total, len - total, 0);
        total += res;
    } while ((res > 0 && total < len) || (res == -1 && errno == EINTR));
    return (total == len ? 1 : 0);
}

static int recv_length (int fd, void *buf, int len) {
    int res;
    int total = 0;

    if (len == 0) return 1;

    do {
        res = recv (fd, (char *) buf + total, len - total, 0);
        total += res;
    } while ((res > 0 && total < len) || (res == -1 && errno == EINTR));
    return (total == len ? 1 : 0);
}

static void dispatch_packet (Package *package) {
    LOGD ("package come : cmd_id %d\n", package->cmd_id);
    mutex_lock (callbask_list_mutex);
    RespCallback *list;
    RespCallbackFunc func = 0;
    for (list = resp_callback_list->next; list; list = list->next) {
        if (list->cmd_id == package->cmd_id) {
            func = list->func;
            break;
        }
    }
    mutex_unlock (callbask_list_mutex);
    if (func) func (package);
}

static void recv_loop (void *args) {
    LOGD ("recv_loop is called...\n");
    while (1) {
        Package header;
        if (!recv_length (_socket_fd, &header, sizeof (header))) { /*sizeof(header)可以不遵循规范, 但gcc和cl都能得到正确结果*/
            close (_socket_fd);
            _socket_fd = 0;
            if (!restart ()) err_callback ();
            break;
        }
        Package *packet = (Package*)malloc (header.all_len);
        packet->all_len = header.all_len;
        packet->cmd_id = header.cmd_id;
        if (!recv_length (_socket_fd, packet->body, packet->all_len - sizeof (header))) {
            close (_socket_fd);
            _socket_fd = 0;
            free (packet);
            if (!restart ()) err_callback ();
            break;
        }
        dispatch_packet (packet); /*分发包到注册的回调函数*/
        free (packet);
    }
    LOGD ("recv_loop return...\n");
}

/**
 * ecmd_init
 *
 * 模块被使用前, 需要调用该函数初始化整个模块
 *
 * @return 成功,返回1(True);失败,返回0(False)
 */
int ecmd_init (ErrCallbackFunc func) {
    clear_err_str ();
    static int inited = 0;
    if (inited == 1) {
        LOGD ("ecmd alreadly inited.");
        return 0;
    }
    inited = 1;

#ifdef _WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        LOGD ("Error at WSAStartup()\n");
        return 0;
    }
#endif

    if (!resp_callback_list) {
        resp_callback_list = (RespCallback*)malloc (sizeof (RespCallback));
        resp_callback_list->next = 0;
    }

    if (!callbask_list_mutex)
        create_mutex (&callbask_list_mutex);

    err_callback = func;
    return 1;
}

static int start_client () {
    SOCKET socket_fd = socket (AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        LOGD ("network_socket create socket_fd err.");
        return 0;
    }

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons (_port);
    if (inet_pton (AF_INET, _ip, &addr.sin_addr.s_addr) != 1) {
        LOGD ("inet_pton error, host : %s", _ip);
        close (socket_fd);
        return 0;
    }

    if (connect (socket_fd, (struct sockaddr*)&addr, sizeof (addr)) == -1) {
        LOGD ("network_connect err : %s", strerror (errno));
        close (socket_fd);
        return 0;
    }

    _socket_fd = socket_fd;
    restart = start_client;

    if (create_thread ((void*)&recv_loop, 0)) {
        LOGD ("create thread error!");
        close (socket_fd);
        _socket_fd = 0;
        return 0;
    }
    return 1;
}

static void _start_client (void *args) {
    if (!start_client ()) err_callback ();
}

/**
 * ecmd_start_client
 * @addr : 目标(服务器)的ip地址
 * @port : 目标(服务器)的ip端口
 *
 * 初始化ecmd的client模式, 连接目标服务器, 启动消息线程
 *
 * @return : 成功,返回1(True);失败,返回0(False),
 * 返回Flase请调用ecmd_error_str()
 */
int ecmd_start_client (char const *ip, uint16_t port) {
    clear_err_str ();
    if (_socket_fd) {
        LOGD ("client has been inited.");
        return 0;
    }

    if (!ip || !port) {
        LOGD ("must set the ip and port.");
        return 0;
    }

    size_t ip_len = strlen (ip) + 1;
    ip_len = (ip_len > 32) ? 32 : ip_len;
    memcpy (_ip, ip, ip_len);
    _port = port;

    if (!create_thread ((void*)&_start_client, 0)) {
        LOGD ("create thread error!");
        return 0;
    }
    return 1;
}

static int start_server () {
    sleep (2);
    SOCKET listen_fd = socket (AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        LOGD ("network_socket create listen_fd err.");
        return 0;
    }

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons (_port);
    if (inet_pton (AF_INET, _ip, &addr.sin_addr.s_addr) != 1) {
        LOGD ("inet_pton error, host : %s", _ip);
        close (listen_fd);
        return 0;
    }

    int yes = 1;
    if (setsockopt (listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        LOGD ("setsockopt error : %s", strerror (errno));
        close (listen_fd);
        return 0;
    }

    if (bind (listen_fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        LOGD ("bind error : %s", strerror (errno));
        close (listen_fd);
        return 0;
    }

    if (listen (listen_fd, 1) != 0) {
        LOGD ("listen error : %s", strerror (errno));
        close (listen_fd);
        return 0;
    }
    LOGD ("waitting for connect...\n");
    SOCKET socket_fd = accept (listen_fd, 0, 0);
    if (socket_fd == -1) {
        LOGD ("accept error : %s", strerror (errno));
        close (listen_fd);
        return 0;
    }
    close (listen_fd);
    LOGD ("connect come!\n");

    _socket_fd = socket_fd;
    restart = start_server;

    yes = 1;
    if (setsockopt (_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        LOGD ("setsockopt error : %s", strerror (errno));
        _socket_fd = 0;
        close (_socket_fd);
        return 0;
    }

    if (!create_thread ((void*)&recv_loop, 0)) {
        LOGD ("create thread error!");
        close (socket_fd);
        _socket_fd = 0;
        return 0;
    }
    return 1;
}

static void _start_server (void *args) {
    if (!start_server ()) err_callback ();
}

/**
 * ecmd_listen_init
 * @addr : 需要监听的ip地址
 * @port : 需要监听的ip端口
 *
 * 初始化ecmd的server模式, 监听指定ip:prot, 等待连接的到来
 *
 * @return : 成功,返回一个EcmdFd,表示一个可用的per端点;失败,返回0(False),
 * 返回Flase请调用ecmd_error_str()
 */
int ecmd_start_server (char const *ip, uint16_t port) {
    clear_err_str ();
    if (_socket_fd) {
        LOGD ("server has been inited.");
        return 0;
    }

    if (!ip || !port) {
        LOGD ("must set the ip and port.");
        return 0;
    }

    size_t ip_len = strlen (ip) + 1;
    ip_len = (ip_len > 32) ? 32 : ip_len;
    memcpy (_ip, ip, ip_len);
    _port = port;

    pthread_t tid;
    if (pthread_create(&tid, NULL, (void *)&_start_server, 0)) {
        LOGD ("create thread error!");
        return 0;
    }
    return 1;
}

/**
 * ecmd_send
 * @cmd_id : 发送的命令ID
 * @data : @cmd_id中附加的数据
 * @len : @data的长度
 *
 * 发送一个指定ID的命令给另一端
 *
 * 这个函数不会设置ecmd_err_str
 */
void ecmd_send (uint32_t cmd_id, uint8_t const *data, size_t len) {
    LOGD ("发送ID : %d", cmd_id);
    if (_socket_fd == 0) return;
    int all_len = sizeof (Package) + len;
    Package *package = (Package*)malloc (all_len);
    package->all_len = all_len;
    package->cmd_id = cmd_id;
    memcpy (package->body, data, len);
    send_length (_socket_fd, (void*)package, all_len);
    free (package);
    return;
}

static void resp_callback_link_add (RespCallback *callback_list, uint32_t cmd_id, RespCallbackFunc func) {
    mutex_lock (callbask_list_mutex);

    RespCallback *list;
    for (list = callback_list->next; list; list = list->next) {
        if (list->cmd_id == cmd_id) {
            list->func = func;
            break;
        }
    }
    if (list) return;

    RespCallback *callback = (RespCallback*)malloc (sizeof (RespCallback));
    callback->cmd_id = cmd_id;
    callback->func = func;
    callback->next = callback_list->next;
    callback_list->next = callback;

    mutex_unlock (callbask_list_mutex);
    LOGD ("%d is added\n", cmd_id);
    return;
}

/**
 * ecmd_register_resp
 * @cmd_id : 发送的命令ID
 * @func : EcmdRespFunc回调函数
 *
 * 注册一个@id指定的回调函数, 当收到@id的包时, 会回调@func函数
 * 如果之前已经注册过@id, 那么第二次注册会覆盖掉之前的注册.
 *
 */
void ecmd_register_resp (uint32_t cmd_id, RespCallbackFunc func) {
    clear_err_str ();
    resp_callback_link_add (resp_callback_list, cmd_id, func);
    dump_resp_callback_list ();
}

/**
 * ecmd_unregister_resp
 * @cmd_id : 需要卸载的命令ID
 */
void ecmd_unregister_resp (uint32_t cmd_id) {
    LOGD ("ecmd_unregister_resp is called.\n");
    clear_err_str ();
    mutex_lock (callbask_list_mutex);
    RespCallback *list, *previous = resp_callback_list;
    for (list = resp_callback_list->next; list; list = list->next) {
        if (list->cmd_id == cmd_id) {
            previous->next = list->next;
            free (list);
            break;
        }
        previous = list;
    }
    dump_resp_callback_list ();
    mutex_unlock (callbask_list_mutex);
    return;
}
