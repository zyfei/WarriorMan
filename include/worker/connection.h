#ifndef _COROUTINE_SOCKET_H
#define _COROUTINE_SOCKET_H

/**
 * 协程化socket
 */
#include "bash.h"

extern zend_class_entry workerman_connection_ce;
extern zend_class_entry *workerman_connection_ce_ptr;

/**
 * 协程化socket结构体
 */
typedef struct {
	int id;
	int sockfd;
	wmString *read_buffer; //读缓冲区
	wmString *write_buffer; //写缓冲区
	zval* _This; //指向当前类的指针
} wmConnection;

//为了通过php对象，找到上面的c++对象
typedef struct {
	wmConnection *connection; //c对象 这个是create产生的
	zend_object std; //php对象
} wmConnectionObject;

wmConnectionObject* wm_connection_fetch_object(zend_object *obj);

zend_object* wm_connection_create_object(zend_class_entry *ce);

/////////////////////////////////////////////////////////////////////////////////


//初始化一个自定义的PHP对象，并且让zsocket这个容器指向自定义对象里面的std对象
void php_wm_init_socket_object(zval *zsocket, wmConnection *socket);

//创建协程套接字
wmConnection * wmConnection_init(int domain, int type,
		int protocol);
//给普通客户端连接使用
wmConnection * wmConnection_init_by_fd(int fd);

//查找
wmConnection* wmConnection_find_by_fd(int fd);

ssize_t wmConnection_recv(wmConnection *socket, int32_t length);

wmString* wmConnection_get_write_buffer(wmConnection *socket);

int wmConnection_bind(wmConnection *socket, char *host, int port);

int wmConnection_listen(wmConnection *socket, int backlog);

wmConnection* wmConnection_accept(wmConnection *socket);

ssize_t wmConnection_send(wmConnection *socket, const void *buf,
		size_t len);

int wmConnection_close(wmConnection *socket);

int wmConnection_free(wmConnection *socket);

#endif
