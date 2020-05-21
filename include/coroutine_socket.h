#ifndef _COROUTINE_SOCKET_H
#define _COROUTINE_SOCKET_H

/**
 * 协程化socket
 */
#include "bash.h"

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

//初始化一个自定义的PHP对象，并且让zsocket这个容器指向自定义对象里面的std对象
void php_wm_init_socket_object(zval *zsocket, wmCoroutionSocket *socket);

//创建协程套接字
wmCoroutionSocket * wmCoroutionSocket_init(int domain, int type,
		int protocol);
//给普通客户端连接使用
wmCoroutionSocket * wmCoroutionSocket_init_by_fd(int fd);

//查找
wmCoroutionSocket* wmCoroutionSocket_find_by_fd(int fd);

ssize_t wmCoroutionSocket_recv(wmCoroutionSocket *socket, int32_t length);

wmString* wmCoroutionSocket_get_write_buffer(wmCoroutionSocket *socket);

int wmCoroutionSocket_bind(wmCoroutionSocket *socket, char *host, int port);

int wmCoroutionSocket_listen(wmCoroutionSocket *socket, int backlog);

wmCoroutionSocket* wmCoroutionSocket_accept(wmCoroutionSocket *socket);

bool wmCoroutionSocket_wait_event(wmCoroutionSocket *socket, int event);

ssize_t wmCoroutionSocket_recv(wmCoroutionSocket *socket);

ssize_t wmCoroutionSocket_send(wmCoroutionSocket *socket, const void *buf,
		size_t len);

int wmCoroutionSocket_close(wmCoroutionSocket *socket);

int wmCoroutionSocket_free(wmCoroutionSocket *socket);

#endif
