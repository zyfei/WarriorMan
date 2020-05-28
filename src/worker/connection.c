#include "connection.h"
#include "coroutine.h"

static long wm_coroutine_socket_last_id = 0;
//
static swHashMap *wm_connections = NULL;

/**
 * 创建一个协程socket
 */
wmConnection * wmConnection_init(int domain, int type, int protocol) {
	if (!wm_connections) {
		wm_connections = swHashMap_new(NULL);
	}

	wmConnection *connection = (wmConnection *) wm_malloc(sizeof(wmConnection));

	bzero(connection, sizeof(wmConnection));
	connection->sockfd = wmSocket_create(domain, type, protocol);
	connection->read_buffer = NULL;
	connection->write_buffer = NULL;
	connection->_This = NULL;

	connection->id = ++wm_coroutine_socket_last_id;
	if (connection->sockfd < 0) {
		return NULL;
	}
	wmSocket_set_nonblock(connection->sockfd);

	//添加到map中
	swHashMap_add_int(wm_connections, connection->sockfd, connection);
	return connection;
}

wmConnection * wmConnection_init_by_fd(int fd) {
	swHashMap_del_int(wm_connections, fd);
	wmConnection *connection = (wmConnection *) wm_malloc(sizeof(wmConnection));
	connection->sockfd = fd;
	connection->read_buffer = NULL;
	connection->write_buffer = NULL;
	connection->_This = NULL;
	connection->id = ++wm_coroutine_socket_last_id;
	wmSocket_set_nonblock(connection->sockfd);

	//添加到map中
	swHashMap_add_int(wm_connections, connection->sockfd, connection);
	return connection;
}

wmString* wmConnection_get_read_buffer(wmConnection *connection) {
	if (!connection->read_buffer) {
		connection->read_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}
	return connection->read_buffer;
}

wmString* wmConnection_get_write_buffer(wmConnection *connection) {
	if (!connection->write_buffer) {
		connection->write_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}
	return connection->write_buffer;
}

wmConnection* wmConnection_find_by_fd(int fd) {
	wmConnection* connection = (wmConnection*) swHashMap_find_int(
			wm_connections, fd);
	return connection;
}

int wmConnection_bind(wmConnection *connection, char *host, int port) {
	return wmSocket_bind(connection->sockfd, host, port);
}

int wmConnection_listen(wmConnection *connection, int backlog) {
	return wmSocket_listen(connection->sockfd, backlog);
}

wmConnection* wmConnection_accept(wmConnection *connection) {
	int connfd = wmSocket_accept(connection->sockfd);
	//如果队列满了，或者读取失败，直接返回
	if (connfd < 0 || errno == EAGAIN) {
		return NULL;
	}
	return wmConnection_init_by_fd(connfd);
//	do {
//		//第一次调用函数stSocket_accept来尝试获取客户端连接
//		connfd = wmSocket_accept(connection->sockfd);
//		// errno == EAGAIN  队列慢了，继续尝试操作
//	} while (connfd < 0 && errno == EAGAIN
//			&& wmConnection_wait_event(connection, WM_EVENT_READ)); //说明此时没有客户端连接，我们就需要等待事件
//	return wmConnection_init_by_fd(connfd);
}

//读取
ssize_t wmConnection_recv(wmConnection *connection, int32_t length) {
	int ret;
	wmConnection_get_read_buffer(connection);
	do {
		//第一次调用函数stSocket_accept来尝试获取客户端连接
		ret = wmSocket_recv(connection->sockfd,
				connection->read_buffer->str + connection->read_buffer->length,
				length, 0);
		if (ret > 0) {
			connection->read_buffer->length += ret;
		}
		// errno == EAGAIN  队列慢了，继续尝试操作
	} while (ret < 0 && errno == EAGAIN
			&& wmConnection_wait_event(connection, WM_EVENT_READ)); //如果不能读，我们就需要等待事件
	return ret;
}

ssize_t wmConnection_send(wmConnection *connection, const void *buf, size_t len) {
	int ret;
	do {
		//第一次调用函数stSocket_accept来尝试获取客户端连接
		ret = wmSocket_send(connection->sockfd, buf, len, 0);
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
	swHashMap_del_int(wm_connections, connection->sockfd);
	ret = wmSocket_close(connection->sockfd);
	connection = NULL;
	return ret;
}

//只有在handle free回调时候调用，因为现在有申请栈重复的情况。
//只释放，不关
int wmConnection_free(wmConnection *connection) {
	int ret = 1;
	if (!connection) {
		return ret;
	}
	swHashMap_del_int(wm_connections, connection->sockfd);
	wm_free(connection->read_buffer);
	wm_free(connection->write_buffer);
	if (connection->_This) {
		efree(connection->_This);
	}
	wm_free(connection);
	connection = NULL;
	return ret;
}

