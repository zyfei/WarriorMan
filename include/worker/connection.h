/**
 * 用以保存客户端的连接，并且提供相应处理函数
 */
#ifndef _COROUTINE_SOCKET_H
#define _COROUTINE_SOCKET_H

#include "base.h"
#include "worker.h"

/**
 * 协程化socket结构体
 */
typedef struct {
	//写入php属性中 start
	int id;
	int fd;
	int maxSendBufferSize; //应用层发送缓冲区
	int maxPackageSize; //接收的最大包包长
	//写入php属性中 end
	wmSocket *socket; //创建的socket对象
	zval _This; //指向当前PHP类的指针
	int _status; //当前连接的状态
	int transport; //TCP还是UDP
	bool _isPaused; //暂停接收消息,只对tcp起作用,默认是false
	wmCoroutine *_pausedCoro; //被暂停的协程

	php_fci_fcc *onMessage;
	php_fci_fcc *onClose;
	php_fci_fcc *onBufferFull;
	php_fci_fcc *onBufferDrain;
	php_fci_fcc *onError;

	wmString *read_packet_buffer; //用来保存返回给用户整个包的缓冲区

	wmWorker *worker; //所属于哪一个worker对象

	bool is_sending; //是否正在发送数据，如果是会将send方法保护起来，其他协程无法调用
} wmConnection;

//为了通过php对象，找到上面的c++对象
typedef struct {
	wmConnection *connection; //c对象 这个是create产生的
	zend_object std; //php对象
} wmConnectionObject;

wmConnectionObject* wm_connection_fetch_object(zend_object *obj);
zend_object* wm_connection_create_object(zend_class_entry *ce);

void wmConnection_init();
void wmConnection_shutdown();
wmConnection* wmConnection_create(wmSocket *socket);
wmConnection* wmConnection_create_udp(int fd);
wmConnection* wmConnection_find_by_fd(int fd);
ssize_t wmConnection_recv(wmConnection *socket, int32_t length);
void wmConnection_read(wmConnection *connection);
void wmConnection_recvfrom(wmConnection *connection, wmSocket *socket);
bool wmConnection_send(wmConnection *connection, const void *buf, size_t len, bool raw);
int wmConnection_destroy(wmConnection *connection);
void wmConnection_free(wmConnection *socket);
void wmConnection_closeConnections();
long wmConnection_getConnectionsNum();
unsigned long wmConnection_getTotalRequestNum();
void wmConnection_consumeRecvBuffer(wmConnection *connection, zend_long length);
char* wmConnection_getRemoteIp(wmConnection *connection);
int wmConnection_getRemotePort(wmConnection *connection);
void wmConnection_pauseRecv(wmConnection *connection);
void wmConnection_resumeRecv(wmConnection *connection);

#endif
