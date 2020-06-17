#include "wm_socket.h"

static bool bufferIsFull(wmSocket *socket);
static void checkBufferWillFull(wmSocket *socket);

wmSocket * wmSocket_create(int domain, int type, int protocol) {
	int fd = wm_socket_create(domain, type, protocol);
	if (fd < 0) {
		return NULL;
	}
	return wmSocket_create_by_fd(fd);
}

wmSocket * wmSocket_create_by_fd(int fd) {
	wmSocket *socket = (wmSocket *) wm_malloc(sizeof(wmSocket));
	socket->fd = fd;
	if ((socket->fp = fdopen(fd, "r")) == NULL) {
		wmWarn("open socketFp fail!");
		return NULL;
	}
	//在这里判断是否已经关闭
	int len = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "/proc/%ld/fd/%d", (long) getpid(), fd);
	socket->fp_path = wmString_dup(WorkerG.buffer_stack->str, len);

	socket->read_buffer = NULL;
	socket->write_buffer = NULL;
	socket->closed = false;
	socket->maxSendBufferSize = 0; //应用层发送缓冲区
	socket->maxPackageSize = 0; //接收的最大包包长
	socket->loop_type = -1;
	socket->transport = NULL;

	socket->onBufferWillFull = NULL;
	socket->onBufferFull = NULL;
	socket->events = WM_EVENT_NULL;

	wm_socket_set_nonblock(socket->fd);
	return socket;
}

/**
 * 尝试从管道中读消息
 */
int wmSocket_recv(wmSocket* socket) {
	if (!socket->read_buffer) {
		socket->read_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}
	if (socket->read_buffer->offset == socket->read_buffer->length) {
		socket->read_buffer->length = 0;
		socket->read_buffer->offset = 0;
	}
	int ret = wm_socket_recv(socket->fd, socket->read_buffer->str + socket->read_buffer->length, WM_BUFFER_SIZE_BIG, 0);
	//连接关闭
	if (ret == 0) {
		socket->closed = true;
		return ret;
	}
	if (ret < 0) {
		return ret;
	}
	socket->read_buffer->length += ret;
	return ret;
}

char* wmScoket_getReadBuffer(wmSocket* socket, int length) {
	return socket->read_buffer->str + socket->read_buffer->offset - length;
}

/**
 * 从buffer中读取一个包长，并且返回一个完整的包长度
 * PS:现在只有tcp，就是有多少读多少
 */
int wmSocket_read(wmSocket* socket) {
	//如果read已经读到最后，那么就把消息重置
	if (socket->read_buffer->offset == socket->read_buffer->length) {
		socket->read_buffer->length = 0;
		socket->read_buffer->offset = 0;
		return 0;
	}
	int len = socket->read_buffer->length - socket->read_buffer->offset;
	socket->read_buffer->offset = socket->read_buffer->offset + len;
	return len;
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
	while (1) {
		//能发多少发多少，不用客气
		ret = wm_socket_send(socket->fd, socket->write_buffer->str + socket->write_buffer->offset, socket->write_buffer->length - socket->write_buffer->offset,
			0);
		//如果发生错误
		if (ret < 0 && errno != EAGAIN) {
			//在这里判断是否已经关闭
			if (access(socket->fp_path->str, F_OK) != 0 || feof(socket->fp) != 0) {
				socket->closed = true;
				return WM_SOCKET_ERROR;
			}
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
	return WM_SOCKET_SUCCESS;
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
	wmString_free(socket->read_buffer);
	wmString_free(socket->write_buffer);
	wm_free(socket);	//释放socket
	socket = NULL;
}
