#include "coroutine.h"
#include "coroutine_socket.h"

using workerman::Coroutine;

long wm_coroutine_socket_last_id = 0;

//
swHashMap *wm_connections = swHashMap_new(NULL);

/**
 * 创建一个协程socket
 */
wmCoroutionSocket * wm_coroution_socket_init(int domain, int type,
		int protocol) {
	wmCoroutionSocket *socket = (wmCoroutionSocket *) wm_malloc(
			sizeof(wmCoroutionSocket));

	bzero(socket, sizeof(wmCoroutionSocket));
	socket->sockfd = wmSocket_create(domain, type, protocol);
	socket->read_buffer = NULL;
	socket->write_buffer = NULL;
	socket->type = type;
	socket->id = ++wm_coroutine_socket_last_id;
	if (socket->sockfd < 0) {
		return NULL;
	}
	wmSocket_set_nonblock(socket->sockfd);

	//添加到map中
	swHashMap_add_int(wm_connections, socket->sockfd, socket);
	return socket;
}

wmCoroutionSocket * wm_coroution_socket_init_by_fd(int fd) {
	swHashMap_del_int(wm_connections, fd);
	wmCoroutionSocket *socket = (wmCoroutionSocket *) wm_malloc(
			sizeof(wmCoroutionSocket));
	socket->sockfd = fd;
	socket->read_buffer = NULL;
	socket->write_buffer = NULL;
	socket->type = -1;
	socket->id = ++wm_coroutine_socket_last_id;
	wmSocket_set_nonblock(socket->sockfd);

	//添加到map中
	swHashMap_add_int(wm_connections, socket->sockfd, socket);
	return socket;
}

wmString* wm_coroution_socket_get_read_buffer(wmCoroutionSocket *socket) {
	if (!socket->read_buffer) {
		socket->read_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}
	return socket->read_buffer;
}

wmString* wm_coroution_socket_get_write_buffer(wmCoroutionSocket *socket) {
	if (!socket->write_buffer) {
		socket->write_buffer = wmString_new(WM_BUFFER_SIZE_BIG);
	}
	return socket->write_buffer;
}

wmCoroutionSocket* wm_coroution_socket_find_by_fd(int fd) {
	wmCoroutionSocket* socket = (wmCoroutionSocket*) swHashMap_find_int(
			wm_connections, fd);
	return socket;
}

int wm_coroution_socket_bind(wmCoroutionSocket *socket, char *host, int port) {
	return wmSocket_bind(socket->sockfd, host, port);
}

int wm_coroution_socket_listen(wmCoroutionSocket *socket, int backlog) {
	return wmSocket_listen(socket->sockfd, backlog);
}

bool wm_coroution_socket_wait_event(wmCoroutionSocket *socket, int event) {
	long id;
	Coroutine* co;
	epoll_event *ev;

//初始化epoll
	if (!WorkerG.poll) {
		init_wmPoll();
	}

//获取当前协程
	co = Coroutine::get_current();
//协程ID
	id = co->get_cid();

	ev = WorkerG.poll->events;

//用来判断这个协程需要等待那种类型的事件，目前是支持READ和WRITE。
	ev->events = (event == WM_EVENT_READ ? EPOLLIN : EPOLLOUT);
//将sockfd和协程id打包
	ev->data.u64 = touint64(socket->sockfd, id);

//注册到全局的epollfd上面。
	epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_ADD, socket->sockfd, ev);

	(WorkerG.poll->event_num)++; // 事件数量+1

//切换协程
	co->yield();

//程序到这里，说明已经执行完毕了。那么应该减去这个事件
	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_DEL, socket->sockfd, NULL)
			< 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}

	(WorkerG.poll->event_num)--; // 事件数量-1
	return true;
}

wmCoroutionSocket* wm_coroution_socket_accept(wmCoroutionSocket *socket) {
	int connfd;
	do {
		//第一次调用函数stSocket_accept来尝试获取客户端连接
		connfd = wmSocket_accept(socket->sockfd);
		// errno == EAGAIN  队列慢了，继续尝试操作
	} while (connfd < 0 && errno == EAGAIN
			&& wm_coroution_socket_wait_event(socket, WM_EVENT_READ)); //说明此时没有客户端连接，我们就需要等待事件
	return wm_coroution_socket_init_by_fd(connfd);
}

//读取
ssize_t wm_coroution_socket_recv(wmCoroutionSocket *socket, int32_t length) {
	int ret;
	wm_coroution_socket_get_read_buffer(socket);
	do {
		//第一次调用函数stSocket_accept来尝试获取客户端连接
		ret = wmSocket_recv(socket->sockfd,
				socket->read_buffer->str + socket->read_buffer->length, length,
				0);
		if (ret > 0) {
			socket->read_buffer->length += ret;
		}
		// errno == EAGAIN  队列慢了，继续尝试操作
	} while (ret < 0 && errno == EAGAIN
			&& wm_coroution_socket_wait_event(socket, WM_EVENT_READ)); //如果不能读，我们就需要等待事件
	return ret;
}

ssize_t wm_coroution_socket_send(wmCoroutionSocket *socket, const void *buf,
		size_t len) {
	int ret;
	do {
		//第一次调用函数stSocket_accept来尝试获取客户端连接
		ret = wmSocket_send(socket->sockfd, buf, len, 0);
		// errno == EAGAIN  队列慢了，继续尝试操作
	} while (ret < 0 && errno == EAGAIN
			&& wm_coroution_socket_wait_event(socket, WM_EVENT_WRITE)); //说明此时没有客户端连接，我们就需要等待事件
	return ret;
}

//只关 不释放
int wm_coroution_socket_close(wmCoroutionSocket *socket) {
	int ret = 1;
	if (!socket) {
		return ret;
	}
	swHashMap_del_int(wm_connections, socket->sockfd);
	ret = wmSocket_close(socket->sockfd);
	socket = NULL;
	return ret;
}

//只有在handle free回调时候调用，因为现在有申请栈重复的情况。
//只释放，不关
int wm_coroution_socket_free(wmCoroutionSocket *socket) {
	int ret = 1;
	if (!socket) {
		return ret;
	}
	swHashMap_del_int(wm_connections, socket->sockfd);
	wm_free(socket->read_buffer);
	wm_free(socket->write_buffer);
	wm_free(socket);
	socket = NULL;
	return ret;
}
