#ifndef _COROUTINE_SOCKET_H
#define _COROUTINE_SOCKET_H
/**
 * 协程化socket
 */
#include "bash.h"

extern long wm_coroutine_socket_last_id;

/**
 * 协程化socket结构体
 */
typedef struct {
	int id;
	int sockfd;
	int type; //是TCP还是UDP
	wmString *read_buffer; //读缓冲区
	wmString *write_buffer; //写缓冲区
} wmCoroutionSocket;

//保存了所有的连接，fd为key,wmCoroutionSocket为value , int的时候填进去，close的时候移出
extern swHashMap *wm_connections;

//创建协程套接字
wmCoroutionSocket * wm_coroution_socket_init(int domain, int type,
		int protocol);
//给普通客户端连接使用
wmCoroutionSocket * wm_coroution_socket_init_by_fd(int fd);

//查找
wmCoroutionSocket* wm_coroution_socket_find_by_fd(int fd);

ssize_t wm_coroution_socket_recv(wmCoroutionSocket *socket, int32_t length);

wmString* wm_coroution_socket_get_write_buffer(wmCoroutionSocket *socket);

int wm_coroution_socket_bind(wmCoroutionSocket *socket, char *host, int port);

int wm_coroution_socket_listen(wmCoroutionSocket *socket, int backlog);

wmCoroutionSocket* wm_coroution_socket_accept(wmCoroutionSocket *socket);

bool wm_coroution_socket_wait_event(wmCoroutionSocket *socket, int event);

ssize_t wm_coroution_socket_recv(wmCoroutionSocket *socket);

ssize_t wm_coroution_socket_send(wmCoroutionSocket *socket, const void *buf,
		size_t len);

int wm_coroution_socket_close(wmCoroutionSocket *socket);

int wm_coroution_socket_free(wmCoroutionSocket *socket);

#endif
