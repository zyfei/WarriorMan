#include "connection.h"

static long wm_coroutine_socket_last_id = 0;
static wmHash_INT_PTR *wm_connections = NULL; //记录着正在连接状态的conn

//检查是否发送缓存区慢
static void bufferWillFull(void *_connection);
static void bufferIsFull(void *_connection);
static int onClose(wmConnection *connection);
//专门给loop回调用的
static void onRead(int fd, int coro_id);
static void onError(int fd, int coro_id);

void wmConnection_init() {
	wm_connections = wmHash_init(WM_HASH_INT_STR);

	wmWorkerLoop_set_handler(WM_EVENT_READ, WM_LOOP_CONNECTION, onRead);
	wmWorkerLoop_set_handler(WM_EVENT_ERROR, WM_LOOP_CONNECTION, onError);
}

wmConnection * wmConnection_create(int fd) {

	//查找并且删除key为fd的连接
	WM_HASH_DEL(WM_HASH_INT_STR, wm_connections, fd);

	wmConnection *connection = (wmConnection *) wm_malloc(sizeof(wmConnection));
	connection->fd = fd;
	connection->socket = wmSocket_create_by_fd(fd);
	if (connection->socket == NULL) {
		return NULL;
	}
	connection->socket->loop_type = WM_LOOP_CONNECTION;
	connection->socket->maxPackageSize = WM_MAX_PACKAGE_SIZE;
	connection->socket->maxSendBufferSize = WM_MAX_SEND_BUFFER_SIZE;
	connection->socket->onBufferFull = bufferIsFull;
	connection->socket->onBufferWillFull = bufferWillFull;
	//绑定Full回调

	connection->_This = NULL;
	connection->id = ++wm_coroutine_socket_last_id;
	connection->_status = WM_CONNECTION_STATUS_ESTABLISHED;

	connection->maxSendBufferSize = WM_MAX_SEND_BUFFER_SIZE;
	connection->maxPackageSize = WM_MAX_PACKAGE_SIZE;

	connection->onMessage = NULL;
	connection->onClose = NULL;
	connection->onBufferFull = NULL;
	connection->onBufferDrain = NULL;
	connection->onError = NULL;
	if (connection->fd < 0) {
		wm_coroutine_socket_last_id = 0;
		connection->id = ++wm_coroutine_socket_last_id;
	}
	if (WM_HASH_ADD(WM_HASH_INT_STR,wm_connections,connection->fd,connection) < 0) {
		wmWarn("wmConnection_create-> connections_add fail");
		return NULL;
	}
	return connection;
}

wmConnection* wmConnection_find_by_fd(int fd) {
	return WM_HASH_GET(WM_HASH_INT_STR, wm_connections, fd);
}

/**
 * onMessage协程结束调用
 */
void onMessage_callback(void* _mess_data) {
	zval* md = (zval*) _mess_data;
	zval* md2 = (zval*) ((char *) _mess_data + sizeof(zval));
	zval_ptr_dtor(md2);
	efree(md);
}

/**
 * onError结束调用
 */
void onError_callback(void* _mess_data) {
	zval* md = (zval*) _mess_data;
	zval* md2 = (zval*) ((char *) _mess_data + (sizeof(zval) * 2));
	zval_ptr_dtor(md2);
	efree(md);
}

//已经可以读消息了
void onRead(int fd, int coro_id) {
	wmConnection* connection = wmConnection_find_by_fd(fd);
	if (connection == NULL) {
		wmWarn("Error has occurred: _wmConnection_onRead fd=%d wmConnection is NULL", fd);
		exit(0);
		return;
	}

	int ret = wmSocket_recv(connection->socket);
	//连接关闭
	if (ret == 0) {
		zend_update_property_long(workerman_connection_ce_ptr, connection->_This, ZEND_STRL("errCode"), WM_ERROR_SESSION_CLOSED_BY_CLIENT);
		zend_update_property_string(workerman_connection_ce_ptr, connection->_This, ZEND_STRL("errMsg"), wmCode_str(WM_ERROR_SESSION_CLOSED_BY_CLIENT));
		onClose(connection);
		return;
	}
	if (ret < 0) {
		//如果由于信号中断或者暂时没有数据可读，直接返回
		if (errno == EINTR || errno == EAGAIN) {
			return;
		}
		wmWarn("Error has occurred: (fd=%d,errno %d) %s", connection->fd, errno, strerror(errno));
		php_error_docref(NULL, E_WARNING, "recv error");
		return;
	}

	//这个socket是否有一个完整的消息,如果有的话，就读出来
	int buffer_len;
	while ((buffer_len = wmSocket_read(connection->socket)) > 0) {
		//创建一个单独协程处理
		if (connection->onMessage) {
			//构建zval，默认的引用计数是1，在php方法调用完毕释放
			zval* _mess_data = (zval*) emalloc(sizeof(zval) * 2);
			_mess_data[0] = *connection->_This;
			zend_string* _zs = zend_string_init(wmScoket_getReadBuffer(connection->socket, buffer_len), buffer_len, 0);
			ZVAL_STR(&_mess_data[1], _zs);
			long _cid = wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
			wmCoroutine_set_callback(_cid, onMessage_callback, _mess_data);
		}
	}
}

/**
 * 发送数据
 */
bool wmConnection_send(wmConnection *connection, const void *buf, size_t len) {
	if (connection->_status == WM_CONNECTION_STATUS_CLOSED || connection->_status == WM_CONNECTION_STATUS_CLOSING) {
		return false;
	}

	int ret = wmSocket_send(connection->socket, buf, len);
	//触发onError
	if (ret == WM_SOCKET_ERROR) {
		onError(connection->fd, 0);
		return false;
	}
	//触发onClose
	if (ret == WM_SOCKET_CLOSE) {
		onClose(connection);
		return false;
	}
	return true;

}

//应用层发送缓冲区是否这次添加之后，已经满了
void bufferWillFull(void *_connection) {
	wmConnection* connection = (wmConnection*) _connection;
	if (connection->onBufferFull) {
		wmCoroutine_create(&(connection->onBufferFull->fcc), 1, connection->_This); //创建新协程
	}
}

//检查是否已经满了,并回调
void bufferIsFull(void *_connection) {
	wmConnection* connection = (wmConnection*) _connection;
	if (connection->onError) {
		//构建zval，默认的引用计数是1，在php方法调用完毕释放
		zval* _mess_data = (zval*) emalloc(sizeof(zval) * 3);
		_mess_data[0] = *connection->_This;
		ZVAL_LONG(&_mess_data[1], WM_ERROR_SEND_FAIL);
		zend_string* _zs = zend_string_init(wmCode_str(WM_ERROR_SEND_FAIL), strlen(wmCode_str(WM_ERROR_SEND_FAIL)), 0);
		ZVAL_STR(&_mess_data[2], _zs);
		long _cid = wmCoroutine_create(&(connection->onError->fcc), 3, _mess_data); //创建新协程
		wmCoroutine_set_callback(_cid, onError_callback, _mess_data);
	}
}

//这是一个用户调用的方法
int wmConnection_close(wmConnection *connection) {
	return onClose(connection);
}

/**
 * 关闭&删除所有的连接
 */
void wmConnection_close_connections() {
	for (int k = wmHash_begin(wm_connections); k != wmHash_end(wm_connections); k++) {
		if (!wmHash_exist(wm_connections, k)) {
			continue;
		}
		wmConnection* conn = wmHash_value(wm_connections, k);
		wmHash_del(WM_HASH_INT_STR, wm_connections, k);
		onClose(conn);
	}
}

/**
 * 处理epoll失败的情况
 */
void onError(int fd, int coro_id) {
	wmConnection* conn = wmConnection_find_by_fd(fd);
	if (conn == NULL) {
		return;
	}
	onClose(conn);
}

/**
 * 关闭这个连接
 */
int onClose(wmConnection *connection) {
	if (connection->_status == WM_CONNECTION_STATUS_CLOSED) {
		return 0;
	}

	wmWorkerLoop_del(connection->fd); //释放事件
	int ret = wmSocket_close(connection->socket);

	connection->_status = WM_CONNECTION_STATUS_CLOSED;
	//触发onClose
	if (connection->onClose) {
		wmCoroutine_create(&(connection->onClose->fcc), 1, connection->_This); //创建新协程
	}

	//查找并且删除key为fd的连接
	WM_HASH_DEL(WM_HASH_INT_STR, wm_connections, connection->fd);

	//释放connection,摧毁这个类，如果顺利的话会触发wmConnection_free
	zval_ptr_dtor(connection->_This);

	return ret;
}

//释放obj和connection的内存
void wmConnection_free(wmConnection *connection) {
	if (!connection) {
		return;
	}
	//如果还在连接，那么调用close
	if (connection->_status == WM_CONNECTION_STATUS_CONNECTING) {
		onClose(connection);
	}
	wmSocket_free(connection->socket);

	//释放暂时申请指向自身php对象的zval指针
	if (connection->_This) {
		efree(connection->_This);
	}
	wm_free(connection);	//释放connection
	connection = NULL;
}

void wmConnection_shutdown() {
	wmHash_destroy(WM_HASH_INT_STR,wm_connections);
}
