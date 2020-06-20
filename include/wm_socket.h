/**
 * 对于socket进行封装
 */
#ifndef _WM_SOCKET_H
#define _WM_SOCKET_H

#include "header.h"
#include "coroutine.h"
#include "socket.h"
#include "wm_string.h"
#include "loop.h"

typedef void (*wm_socket_func_t)(void*);

typedef struct {
	int fd; //文件描述符
	wmString *write_buffer; //写缓冲区
	int maxSendBufferSize; //应用层发送缓冲区
	int maxPackageSize; //接收的最大包包长
	int events; //loop监听了什么事件
	bool closed; //是否关闭
	void* owner; //拥有人，比如connection创建的socket，owner就是这个connection
	int errCode; //错误码
	const char *errMsg; //错误描述

	/**
	 * worker和connection类型会自己管理loop。runtime是在read或者write的时候代为管理
	 */
	int loop_type; //对应wmLoop_type这个枚举
	int transport; //什么协议类型，比如TCP UDP等
	wm_socket_func_t onBufferWillFull;
	char* connect_host;
	int connect_port;

	/**
	 * runtime用
	 */
	bool shutdown_read;
	bool shutdown_write;
} wmSocket;

wmSocket * wmSocket_create(int transport);
wmSocket * wmSocket_create_by_fd(int fd, int transport);
int wmSocket_read(wmSocket* socket, char *buf, int len);
int wmSocket_send(wmSocket *socket, const void *buf, size_t len);
int wmSocket_write(wmSocket *socket, const void *buf, size_t len);//不管缓冲区
int wmSocket_close(wmSocket *socket);
void wmSocket_free(wmSocket *socket);
bool wmSocket_connect(wmSocket *socket, char* _host, int _port);
wmSocket * wmSocket_accept(wmSocket* socket);
ssize_t wmSocket_peek(wmSocket* socket, void *__buf, size_t __n);
bool wmSocket_shutdown(wmSocket *socket, int __how);
ssize_t wmSocket_recvfrom(wmSocket* socket, void *__buf, size_t __n, struct sockaddr* _addr, socklen_t *_socklen);
#endif
