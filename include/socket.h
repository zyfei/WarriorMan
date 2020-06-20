/**
 * 基础的socket操作
 */
#ifndef _SOCKET_H
#define _SOCKET_H

#include "header.h"
#include "log.h"

int wm_socket_create(int domain, int type, int protocol); //创建套接字
int wm_socket_set_nonblock(int sock); //设置为非阻塞
int wm_socket_listen(int sock, int backlog); //监听
int wm_socket_bind(int sock, char *host, int port);
int wm_socket_connect(int sock, char *host, int port);

/**
 * 获取客户端连接
 */
int wm_socket_accept(int sock);
/**
 * 获取客户端数据
 */
ssize_t wm_socket_recv(int sock, void *buf, size_t len, int flag);

/**
 * 发送数据
 */
ssize_t wm_socket_send(int sock, const void *buf, size_t len, int flag);
/**
 * 关闭
 */
int wm_socket_close(int fd);

#endif
