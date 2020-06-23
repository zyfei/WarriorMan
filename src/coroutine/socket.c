#include "wm_socket.h"
#include "loop.h"

static bool bufferIsFull(wmSocket *socket);
static void checkBufferWillFull(wmSocket *socket);
static bool event_wait(wmSocket* socket, int event);

/**
 * 设置socket的各种错误
 */
static inline void set_err(wmSocket* socket, int e) {
	socket->errCode = errno = e;
	socket->errMsg = e ? wmCode_str(e) : "";
}

/**
 * 创建一个socket对象
 */
wmSocket * wmSocket_create(int transport, int loop_type) {
	int fd = -1;
	switch (transport) {
	case WM_SOCK_TCP:
		fd = wm_socket_create(AF_INET, SOCK_STREAM, 0);
	}
	if (fd < 0) {
		return NULL;
	}
	return wmSocket_pack(fd, transport, loop_type);
}

/**
 * 包装一个socket对象
 */
wmSocket * wmSocket_pack(int fd, int transport, int loop_type) {
	wmSocket *socket = (wmSocket *) wm_malloc(sizeof(wmSocket));
	socket->fd = fd;

	socket->write_buffer = NULL;
	socket->closed = false;
	socket->maxSendBufferSize = 0; //应用层发送缓冲区
	socket->maxPackageSize = 0; //接收的最大包包长
	socket->loop_type = loop_type;
	socket->transport = transport;

	socket->read_co = NULL;
	socket->write_co = NULL;

	socket->connect_host = NULL;
	socket->connect_port = 0;

	socket->onBufferWillFull = NULL;
	socket->events = WM_EVENT_NULL;
	socket->errCode = 0; //默认没有错误
	socket->errMsg = NULL;
	socket->shutdown_read = false;
	socket->shutdown_write = false;

	wm_socket_set_nonblock(socket->fd);
	return socket;
}

/**
 * 等待连接
 */
wmSocket * wmSocket_accept(wmSocket* socket, int new_socket_loop_type) {
	int connfd;
	while (!socket->closed) {
		do {
			connfd = wm_socket_accept(socket->fd);
		} while (connfd < 0 && errno == EINTR);
		if (connfd < 0) {
			if (errno != EAGAIN) {
				set_err(socket, errno);
				wmWarn("wmSocket_accept fail. %s", socket->errMsg);
				return NULL;
			}
			if (!event_wait(socket, WM_EVENT_READ)) {
				set_err(socket, WM_ERROR_LOOP_FAIL);
				wmWarn("wmSocket_accept fail. %s", socket->errMsg);
				return NULL;
			}
			continue;
		}
		set_err(socket, 0);
		return wmSocket_pack(connfd, socket->transport, new_socket_loop_type); //将得到的fd，包装成socket结构体
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return NULL;
}

/**
 * 主动连接
 */
bool wmSocket_connect(wmSocket *socket, char* _host, int _port) {
	int retval;
	if (!socket->closed) {
		do {
			retval = wm_socket_connect(socket->fd, _host, _port);
		} while (retval < 0 && errno == EINTR);
		if (retval < 0) {
			if (errno != EINPROGRESS) {
				set_err(socket, errno);
				wmWarn("wmSocket_connect fail. host=%s port=%d errno=%d", _host, _port, errno);
				return false;
			}
			if (!event_wait(socket, WM_EVENT_WRITE)) {
				set_err(socket, WM_ERROR_LOOP_FAIL);
				return false;
			}
			//这里需要使用epoll监听可写事件，然后还需要有一个超时设置，
			socklen_t len = sizeof(socket->errCode);
			if (getsockopt(socket->fd, SOL_SOCKET, SO_ERROR, &socket->errCode, &len) < 0 || socket->errCode != 0) {
				set_err(socket, socket->errCode);
				return false;
			}
		}
		socket->connect_host = _host;
		socket->connect_port = _port;
		set_err(socket, 0);
		return retval;
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return false;
}

/**
 * 偷看一下之前读取的数据
 * 暂时只有runtime用
 */
ssize_t wmSocket_peek(wmSocket* socket, void *__buf, size_t __n) {
	ssize_t retval;
	int __flags = MSG_PEEK;
	do {
		retval = recv(socket->fd, __buf, __n, __flags);
	} while (retval < 0 && errno == EINTR);

	wmTrace("peek %ld/%ld bytes, errno=%d", retval, __n, errno);
	set_err(socket, retval < 0 ? errno : 0);
	return retval;
}

/**
 * 读数据
 * 暂时只有runtime用
 */
ssize_t wmSocket_recvfrom(wmSocket* socket, void *__buf, size_t __n, struct sockaddr* _addr, socklen_t *_socklen) {
	ssize_t retval;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	struct sockaddr_in servaddr;
	do {
		bzero(&servaddr, sizeof(servaddr));
		retval = recvfrom(socket->fd, __buf, __n, 0, (struct sockaddr*) &servaddr, &addr_len);

		wmTrace("recvfrom %ld/%ld bytes, errno=%d", retval, __n, errno);
	} while (retval < 0 && ((errno == EINTR) || (errno == 0 && event_wait(socket, WM_EVENT_READ))));
	set_err(socket, retval < 0 ? errno : 0);
	return retval;
}

/**
 * 读数据
 */
int wmSocket_read(wmSocket* socket, char *buf, int len) {
	int ret;
	while (!socket->closed) {
		do {
			ret = wm_socket_recv(socket->fd, buf, len, 0);
		} while (ret < 0 && errno == EINTR);
		//正常返回
		if (ret > 0) {
			return ret;
		}
		//连接关闭
		if (ret == 0) {
			socket->closed = true;
			set_err(socket, WM_ERROR_SESSION_CLOSED_BY_CLIENT);
			return WM_SOCKET_CLOSE;
		}
		if (ret < 0 && errno == EAGAIN) {
			if (event_wait(socket, WM_EVENT_READ)) {
				continue;
			}
		}
		set_err(socket, WM_ERROR_READ_FAIL);
		return WM_SOCKET_ERROR;
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return WM_SOCKET_CLOSE;
}

/**
 * 所有写入都是协程同步的
 */
int wmSocket_send(wmSocket *socket, const void *buf, size_t len) {
	if (socket->closed == true) {
		set_err(socket, WM_ERROR_SESSION_CLOSED);
		return WM_SOCKET_CLOSE;
	}
	if (!socket->write_buffer) {
		socket->write_buffer = wmString_new(WM_BUFFER_SIZE_DEFAULT); //默认是1024
	}
	if (socket->write_buffer->offset == socket->write_buffer->length) {
		socket->write_buffer->length = 0;
		socket->write_buffer->offset = 0;
	}
	if (bufferIsFull(socket)) {
		set_err(socket, WM_ERROR_SEND_BUFFER_FULL); //发送区满了
		return WM_SOCKET_ERROR;
	}
	wmString_append_ptr(socket->write_buffer, buf, len); //要发送的字符，添加进去
	checkBufferWillFull(socket); //检查这一次加完，会不会缓冲区满

	int ret;
	int ret_num = 0; //一共发送多少字节
	while (!socket->closed) {
		//能发多少发多少，不用客气
		do {
			ret = wm_socket_send(socket->fd, socket->write_buffer->str + socket->write_buffer->offset,
				socket->write_buffer->length - socket->write_buffer->offset, 0);
		} while (ret < 0 && errno == EINTR);

		//如果是未知错误，我们检查socket是否关闭
		if (ret < 0 && errno != EAGAIN) {
			//在这里判断是否已经关闭
			socklen_t len = sizeof(socket->errCode);
			if (getsockopt(socket->fd, SOL_SOCKET, SO_ERROR, &socket->errCode, &len) < 0 || socket->errCode != 0) {
				set_err(socket, socket->errCode);
				socket->closed = true;
				return WM_SOCKET_CLOSE;
			}
			ret = 0;
		}
		socket->write_buffer->offset += ret;
		ret_num += ret;

		if (socket->write_buffer->offset == socket->write_buffer->length) {
			wmWorkerLoop_remove(socket, WM_EVENT_WRITE);
			socket->write_buffer->offset = 0;
			socket->write_buffer->length = 0;
			return ret_num;
		}
		event_wait(socket, WM_EVENT_WRITE);
	}
	wmWorkerLoop_remove(socket, WM_EVENT_WRITE);
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return WM_SOCKET_CLOSE;
}

/**
 * 全发送,不管缓冲区
 */
int wmSocket_write(wmSocket *socket, const void *buf, size_t len) {
	if (socket->closed == true) {
		set_err(socket, WM_ERROR_SESSION_CLOSED);
		return WM_SOCKET_CLOSE; //表示关闭
	}
	int ret;
	int ret_num = 0;
	while (!socket->closed) {
		//能发多少发多少，不用客气
		do {
			ret = wm_socket_send(socket->fd, buf, len - ret_num, 0);
		} while (ret < 0 && errno == EINTR);
		//如果发生错误
		if (ret < 0 && errno != EAGAIN) {
			//在这里判断是否已经关闭
			socklen_t len = sizeof(socket->errCode);
			if (getsockopt(socket->fd, SOL_SOCKET, SO_ERROR, &socket->errCode, &len) < 0 || socket->errCode != 0) {
				set_err(socket, socket->errCode);
				socket->closed = true;
				return WM_SOCKET_CLOSE;
			}
			ret = 0;
		}
		ret_num += ret;
		if (ret_num >= len) {
			wmWorkerLoop_remove(socket, WM_EVENT_WRITE);
			return ret_num;
		}
		event_wait(socket, WM_EVENT_WRITE);
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return WM_SOCKET_CLOSE;
}

/**
 * 不同的loop_type操作是不同的
 */
bool event_wait(wmSocket* socket, int event) {
	//如果没有事件监听,就加上
	if (!(socket->events & event)) {
		if (!wmWorkerLoop_add(socket, event)) {
			return false;
		}
	}
	if (event & WM_EVENT_READ) {
		socket->read_co = wmCoroutine_get_current();
	}
	if (event & WM_EVENT_WRITE) {
		socket->write_co = wmCoroutine_get_current();
	}

	wmCoroutine_yield();
	return true;
}

//检查写缓冲区是不是已经满了
bool bufferIsFull(wmSocket *socket) {
	if (socket->maxSendBufferSize <= (socket->write_buffer->length - socket->write_buffer->offset)) {
		return true;
	}
	return false;
}

//检查应用层发送缓冲区是否这次添加之后，已经满了
void checkBufferWillFull(wmSocket *socket) {
	if (socket->maxSendBufferSize <= (socket->write_buffer->length - socket->write_buffer->offset)) {
		if (socket->onBufferWillFull) {
			if (socket->onBufferWillFull) {
				socket->onBufferWillFull(socket->owner);
			}
		}
	}
}

/**
 * runtime用
 */
bool wmSocket_shutdown(wmSocket *socket, int __how) {
	set_err(socket, 0);
	if ((socket->closed && socket->shutdown_read && socket->shutdown_write) || (__how == SHUT_RD && socket->shutdown_read)
		|| (__how == SHUT_WR && socket->shutdown_write)) {
		errno = ENOTCONN;
	} else {
		if (errno == ENOTCONN) {
			// connection reset by server side
			__how = SHUT_RDWR;
		}
		switch (__how) {
		case SHUT_RD:
			socket->shutdown_read = true;
			break;
		case SHUT_WR:
			socket->shutdown_write = true;
			break;
		default:
			socket->shutdown_read = socket->shutdown_write = true;
			break;
		}
		return true;
	}
	set_err(socket, errno);
	return false;
}

/**
 * 关闭这个连接
 */
int wmSocket_close(wmSocket *socket) {
	if (socket->closed == true) {
		return 0;
	}
	if (socket->events != WM_EVENT_NULL) {
		wmWorkerLoop_del(socket); //释放事件
	}

	int ret = wm_socket_close(socket->fd);
	socket->closed = true;
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return ret;
}

/**
 * 释放申请的内存
 */
void wmSocket_free(wmSocket *socket) {
	if (!socket) {
		return;
	}
	if (socket->events != WM_EVENT_NULL) {
		wmWorkerLoop_del(socket);
	}
	//如果还在连接，那么调用close
	if (socket->closed == false) {
		wmSocket_close(socket);
	}
	if (socket->write_buffer) {
		wmString_free(socket->write_buffer);
	}
	if (socket->events != WM_EVENT_NULL) {
		wmWorkerLoop_del(socket); //释放事件
	}
	wm_free(socket);	//释放socket
	socket = NULL;
}
