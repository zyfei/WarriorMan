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
	FILE* fp; //文件指针
	wmString* fp_path; //文件具体位置
	wmString *read_buffer; //读缓冲区
	wmString *write_buffer; //写缓冲区
	int maxSendBufferSize; //应用层发送缓冲区
	int maxPackageSize; //接收的最大包包长
	int events; //loop监听了什么事件
	bool closed; //是否关闭
	void* owner; //拥有人，比如connection创建的socket，owner就是这个connection
	int loop_type; //对应wmLoop_type这个枚举
	char* transport;
	wm_socket_func_t onBufferWillFull;
	wm_socket_func_t onBufferFull;
} wmSocket;

wmSocket * wmSocket_create(int domain, int type, int protocol);
wmSocket * wmSocket_create_by_fd(int fd);
int wmSocket_recv(wmSocket* socket);
char* wmScoket_getReadBuffer(wmSocket* socket, int length);
int wmSocket_read(wmSocket* socket);
int wmSocket_send(wmSocket *socket, const void *buf, size_t len);
int wmSocket_close(wmSocket *socket);
void wmSocket_free(wmSocket *socket);

#endif
