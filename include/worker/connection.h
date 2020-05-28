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
	int fd;
	wmString *read_buffer; //读缓冲区
	wmString *write_buffer; //写缓冲区
	zval* _This; //指向当前类的指针

	php_fci_fcc *onMessage;
	php_fci_fcc *onClose;
	php_fci_fcc *onBufferFull;
	php_fci_fcc *onBufferDrain;
	php_fci_fcc *onError;

	void* worker; //派生worker的指针
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

//查找
wmConnection* wmConnection_find_by_fd(int fd);

ssize_t wmConnection_recv(wmConnection *socket, int32_t length);

wmConnection* wmConnection_accept(uint32_t fd);

ssize_t wmConnection_send(wmConnection *socket, const void *buf, size_t len);

int wmConnection_close(wmConnection *socket);

void wmConnection_free(wmConnection *socket);

#endif
