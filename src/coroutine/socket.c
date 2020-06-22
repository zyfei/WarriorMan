#include "wm_socket.h"

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

wmSocket * wmSocket_create(int transport) {
	int fd = -1;
	switch (transport) {
	case WM_SOCK_TCP:
		fd = wm_socket_create(AF_INET, SOCK_STREAM, 0);
	}
	if (fd < 0) {
		return NULL;
	}
	return wmSocket_create_by_fd(fd, transport);
}

wmSocket * wmSocket_create_by_fd(int fd, int transport) {
	wmSocket *socket = (wmSocket *) wm_malloc(sizeof(wmSocket));
	socket->fd = fd;

	socket->write_buffer = NULL;
	socket->closed = false;
	socket->maxSendBufferSize = 0; //应用层发送缓冲区
	socket->maxPackageSize = 0; //接收的最大包包长
	socket->loop_type = -1;
	socket->transport = transport;

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

wmSocket * wmSocket_accept(wmSocket* socket) {
	int connfd;
	while (!socket->closed) {
		do {
			connfd = wm_socket_accept(socket->fd);
		} while (connfd < 0 && errno == EINTR);
		if (connfd < 0) {
			if (errno != EAGAIN) {
				set_err(socket, errno);
				wmWarn("wmSocket_accept fail. %s", socket->errMsg);
				return false;
			}
			if (!event_wait(socket, WM_EVENT_READ)) {
				set_err(socket, WM_ERROR_SESSION_CLOSED);
				return NULL;
			}
			continue;
		}
		set_err(socket, 0);
		wmSocket* socket2 = wmSocket_create_by_fd(connfd, socket->transport);
		return socket2;
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return NULL;
}

/**
 * 作为客户端连接
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
				set_err(socket, WM_ERROR_SESSION_CLOSED);
				return NULL;
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
 * runtime用，自己不用。偷看一下之前读取的数据
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
 * 读消息,读出的消息放入buf中，返回读的长度
 */
int wmSocket_read(wmSocket* socket, char *buf, int len) {
	if (socket->closed == true) {
		set_err(socket, WM_ERROR_SESSION_CLOSED);
		return WM_SOCKET_CLOSE; //表示关闭
	}
	//在这里要通过LOOP TYPE来判断怎么处理

	while (!socket->closed) {
		int ret = wm_socket_recv(socket->fd, buf, len, 0);
		//连接关闭
		if (ret == 0) {
			socket->closed = true;
			set_err(socket, WM_ERROR_SESSION_CLOSED_BY_CLIENT);
			return WM_SOCKET_CLOSE;
		}
		if (ret < 0 && errno != EAGAIN && errno != EINTR) {
			//在这里判断是否已经关闭
			socklen_t len = sizeof(socket->errCode);
			if (getsockopt(socket->fd, SOL_SOCKET, SO_ERROR, &socket->errCode, &len) < 0 || socket->errCode != 0) {
				set_err(socket, socket->errCode);
				socket->closed = true;
				return WM_SOCKET_CLOSE;
			}
		}
		if (ret > 0) {
			return ret;
		}
		event_wait(socket, WM_EVENT_READ);
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return WM_SOCKET_CLOSE;
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
 * 写入是协程同步的
 * 小于0是有问题的，等于0没事，大于0正常
 */
int wmSocket_send(wmSocket *socket, const void *buf, size_t len) {
	if (socket->closed == true) {
		set_err(socket, WM_ERROR_SESSION_CLOSED);
		return WM_SOCKET_CLOSE; //表示关闭
	}
	if (!socket->write_buffer) {
		socket->write_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}

	if (socket->write_buffer->offset == socket->write_buffer->length) {
		socket->write_buffer->length = 0;
		socket->write_buffer->offset = 0;
	}
	int ret_num = 0;
	if (bufferIsFull(socket)) {
		set_err(socket, WM_ERROR_SEND_BUFFER_FULL);
		return WM_SOCKET_ERROR;
	}
	//要发送的字符，添加进去
	wmString_append_ptr(socket->write_buffer, buf, len);

	//检查这一次加完，会不会缓冲区满
	checkBufferWillFull(socket);

	//尝试写
	bool _add_Loop = false;
	int ret;
	while (!socket->closed) {
		//能发多少发多少，不用客气
		ret = wm_socket_send(socket->fd, socket->write_buffer->str + socket->write_buffer->offset, socket->write_buffer->length - socket->write_buffer->offset,
			0);
		//如果发生错误
		if (ret < 0 && errno != EAGAIN && errno != EINTR) {
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
		//发送完了，就直接返回
		if (socket->write_buffer->offset == socket->write_buffer->length) {
			//在这里取消事件注册，使用修改的方式
			if (_add_Loop) {
				socket->events &= (~WM_EVENT_WRITE);
				wmWorkerLoop_update(socket->fd, socket->events, socket->loop_type);
			}
			socket->write_buffer->offset = 0;
			socket->write_buffer->length = 0;
			return ret_num;
		}
		if (!_add_Loop) {
			socket->events |= WM_EVENT_WRITE;
			wmWorkerLoop_update(socket->fd, socket->events, socket->loop_type);
			_add_Loop = true;
		}
		//可写的时候，自动唤醒
		event_wait(socket, WM_EVENT_READ);
	}
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
	//尝试写
	bool _add_Loop = false;
	int ret;
	int ret_num = 0;
	while (!socket->closed) {
		//能发多少发多少，不用客气
		ret = wm_socket_send(socket->fd, buf, len - ret_num, 0);
		//如果发生错误
		if (ret < 0 && errno != EAGAIN && errno != EINTR) {
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
			//在这里取消事件注册，使用修改的方式
			if (_add_Loop) {
				socket->events = socket->events - WM_EVENT_WRITE;
				wmWorkerLoop_update(socket->fd, socket->events, socket->loop_type);
			}
			return ret_num;
		}
		if (!_add_Loop) {
			socket->events |= WM_EVENT_WRITE;
			wmWorkerLoop_update(socket->fd, socket->events, socket->loop_type);
			_add_Loop = true;
		}
		//可写的时候，自动唤醒
		event_wait(socket, WM_EVENT_READ);
	}
	set_err(socket, WM_ERROR_SESSION_CLOSED);
	return WM_SOCKET_CLOSE;
}

/**
 * 不同的loop_type操作是不同的
 */
bool event_wait(wmSocket* socket, int event) {
	/**
	 * runtime中是不同的，只有每次调用read或者相关方法的时候，才会去监听
	 */
	if (socket->loop_type == WM_LOOP_RUNTIME) {
		if (!wmWorkerLoop_add(socket->fd, event, socket->loop_type)) {
			return false;
		}
	}
	wmCoroutine_yield();
	if (socket->loop_type == WM_LOOP_RUNTIME) {
		wmWorkerLoop_del(socket->fd); //删除监听
	}
	return true;
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
	//如果还在连接，那么调用close
	if (socket->closed == false) {
		wmSocket_close(socket);
	}
	if (socket->write_buffer) {
		wmString_free(socket->write_buffer);
	}
	wm_free(socket);	//释放socket
	socket = NULL;
}
