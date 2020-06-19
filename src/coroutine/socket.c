#include "wm_socket.h"

static bool bufferIsFull(wmSocket *socket);
static void checkBufferWillFull(wmSocket *socket);

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
	//在这里判断是否已经关闭
	int len = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "/proc/%ld/fd/%d", (long) getpid(), fd);
	socket->fp_path = wmString_dup(WorkerG.buffer_stack->str, len);

	socket->write_buffer = NULL;
	socket->closed = false;
	socket->maxSendBufferSize = 0; //应用层发送缓冲区
	socket->maxPackageSize = 0; //接收的最大包包长
	socket->loop_type = -1;
	socket->transport = transport;

	socket->onBufferWillFull = NULL;
	socket->onBufferFull = NULL;
	socket->events = WM_EVENT_NULL;

	wm_socket_set_nonblock(socket->fd);
	return socket;
}

/**
 * 读消息,读出的消息放入buf中，返回读的长度
 */
int wmSocket_read(wmSocket* socket, char *buf, int len) {
	if (socket->closed == true) {
		return WM_SOCKET_CLOSE; //表示关闭
	}
	while (!socket->closed) {
		int ret = wm_socket_recv(socket->fd, buf, len, 0);
		//连接关闭
		if (ret == 0) {
			socket->closed = true;
			return WM_SOCKET_CLOSE;
		}
		if (ret < 0 && errno != EAGAIN && errno != EINTR) {
			//在这里判断是否已经关闭
			if (access(socket->fp_path->str, F_OK) != 0) {
				socket->closed = true;
				return WM_SOCKET_ERROR;
			}
		}
		if (ret > 0) {
			return ret;
		}
		wmCoroutine_yield();
	}
	return WM_SOCKET_CLOSE;
}

//检查写缓冲区是不是已经满了,并回调
bool bufferIsFull(wmSocket *socket) {
	if (socket->maxSendBufferSize <= (socket->write_buffer->length - socket->write_buffer->offset)) {
		if (socket->onBufferFull) {
			socket->onBufferFull(socket->owner);
		}
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
 */
int wmSocket_send(wmSocket *socket, const void *buf, size_t len) {
	if (socket->closed == true) {
		return WM_SOCKET_CLOSE; //表示关闭
	}
	if (!socket->write_buffer) {
		socket->write_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}

	if (socket->write_buffer->offset == socket->write_buffer->length) {
		socket->write_buffer->length = 0;
		socket->write_buffer->offset = 0;
	}

	if (bufferIsFull(socket)) {
		return WM_SOCKET_SUCCESS;
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
			if (access(socket->fp_path->str, F_OK) != 0) {
				socket->closed = true;
				return WM_SOCKET_ERROR;
			}
			ret = 0;
		}
		socket->write_buffer->offset += ret;

		//发送完了，就直接返回
		if (socket->write_buffer->offset == socket->write_buffer->length) {

			//在这里取消事件注册，使用修改的方式
			if (_add_Loop) {
				socket->events = socket->events - WM_EVENT_WRITE;
				wmWorkerLoop_update(socket->fd, socket->events, socket->loop_type);
			}
			socket->write_buffer->offset = 0;
			socket->write_buffer->length = 0;
			return WM_SOCKET_SUCCESS;
		}
		if (!_add_Loop) {
			socket->events |= WM_EVENT_WRITE;
			wmWorkerLoop_update(socket->fd, socket->events, socket->loop_type);
			_add_Loop = true;
		}
		//可写的时候，自动唤醒
		wmCoroutine_yield();
	}
	return WM_SOCKET_CLOSE;
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
	if (socket->fp_path) {
		wmString_free(socket->fp_path);
	}
	if (socket->write_buffer) {
		wmString_free(socket->write_buffer);
	}
	wm_free(socket);	//释放socket
	socket = NULL;
}
