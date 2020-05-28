#include "connection.h"
#include "coroutine.h"
#include "loop.h"

static long wm_coroutine_socket_last_id = 0;
//
static swHashMap *wm_connections = NULL;

wmConnection * create(int fd);
void baseRead(void * _conn);

wmConnection * create(int fd) {
	if (!wm_connections) {
		wm_connections = swHashMap_new(NULL);
	}
	swHashMap_del_int(wm_connections, fd);
	wmConnection *connection = (wmConnection *) wm_malloc(sizeof(wmConnection));
	connection->fd = fd;
	connection->read_buffer = NULL;
	connection->write_buffer = NULL;
	connection->_This = NULL;
	connection->id = ++wm_coroutine_socket_last_id;
	if (connection->fd < 0) {
		return NULL;
	}

	wmSocket_set_nonblock(connection->fd);

	//添加到map中
	swHashMap_add_int(wm_connections, connection->fd, connection);
	return connection;
}

wmConnection* wmConnection_find_by_fd(int fd) {
	wmConnection* connection = (wmConnection*) swHashMap_find_int(
			wm_connections, fd);
	return connection;
}

wmConnection* wmConnection_accept(uint32_t fd) {
	int connfd = wmSocket_accept(fd);
	//如果队列满了，或者读取失败，直接返回
	if (connfd < 0 || errno == EAGAIN) {
		return NULL;
	}
	wmConnection* conn = create(connfd);
	if (!conn) {
		return NULL;
	}
	//添加读监听
	wmWorkerLoop_add(conn->fd, WM_EVENT_READ, baseRead, conn);
	return conn;
}

//已经可以读消息了
void baseRead(void * _conn) {
	wmConnection* connection = (wmConnection *) _conn;
	if (!connection->read_buffer) {
		connection->read_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}
	int ret = wmSocket_recv(connection->fd,
			connection->read_buffer->str + connection->read_buffer->length,
			WM_BUFFER_SIZE_BIG, 0);
	//连接关闭
	if (ret == 0) {
		zend_update_property_long(workerman_connection_ce_ptr,
				connection->_This, ZEND_STRL("errCode"),
				WM_ERROR_SESSION_CLOSED_BY_CLIENT);

		zend_update_property_string(workerman_connection_ce_ptr,
				connection->_This, ZEND_STRL("errMsg"),
				wmCode_str(WM_ERROR_SESSION_CLOSED_BY_CLIENT));
		wmConnection_close(connection);
		return;
	}
	if (ret > 0) {
		connection->read_buffer->length += ret;
	}

	if (connection->read_buffer->length == 0) {
		return;
	}

	php_printf("shen qing %d \n", connection->id);

	if (connection->onMessage) {
		//创建一个单独协程处理
		zval* _mess_data = (zval*) emalloc(sizeof(zval) * 2);
		_mess_data[0] = *connection->_This;
		//理论上运行完，php会自动释放
		ZVAL_STR(&_mess_data[1],
				zend_string_init(connection->read_buffer->str,
						connection->read_buffer->length, 0));
		//现在就可以清空read）bufferle
		connection->read_buffer->length = 0;
		wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
	}
}

ssize_t wmConnection_send(wmConnection *connection, const void *buf, size_t len) {
	int ret;
	do {
		//第一次调用函数stSocket_accept来尝试获取客户端连接
		ret = wmSocket_send(connection->fd, buf, len, 0);
		// errno == EAGAIN  队列慢了，继续尝试操作
	} while (ret < 0 && errno == EAGAIN
			&& wmConnection_wait_event(connection, WM_EVENT_WRITE)); //说明此时没有客户端连接，我们就需要等待事件
	return ret;
}

//只关 不释放
int wmConnection_close(wmConnection *connection) {
	int ret = 1;
	if (!connection) {
		return ret;
	}
	ret = wmSocket_close(connection->fd);
	wmConnection_free(connection);
	return ret;
}

//只有在handle free回调时候调用，因为现在有申请栈重复的情况。
//只释放，不关
void wmConnection_free(wmConnection *connection) {
	//释放过就不释放了
	if (!connection) {
		return;
	}
	wmWorkerLoop_del(connection->fd); //释放事件

	//调用onClose

	swHashMap_del_int(wm_connections, connection->fd); //从hash表删除
	wm_free(connection->read_buffer); //释放
	wm_free(connection->write_buffer); //释放
	//释放暂时申请指向自身php对象的zval指针
	if (connection->_This) {
		efree(connection->_This);
	}
	wm_free(connection);	//释放connection
	connection = NULL;
}

