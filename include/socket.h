#ifndef _SOCKET_H
#define _SOCKET_H

#include "header.h"
#include "log.h"

int wmSocket_create(int domain, int type, int protocol);//创建套接字
int wmSocket_set_nonblock(int sock); //设置为非阻塞
int wmSocket_listen(int sock, int backlog); //监听
int wmSocket_bind(int sock, char *host, int port);
/**
 * 获取客户端连接
 */
int wmSocket_accept(int sock);
/**
 * 获取客户端数据
 */
ssize_t wmSocket_recv(int sock, void *buf, size_t len, int flag);

/**
 * 发送数据
 */
ssize_t wmSocket_send(int sock, const void *buf, size_t len, int flag);
/**
 * 关闭
 */
int wmSocket_close(int fd);

#endif
