#include "loop.h"
#include "coroutine.h"
#include "socket.h"
#include "log.h"

static loop_callback_func_t read_handler[7];
static loop_callback_func_t write_handler[7];

//把我们自己的events转换成epoll的
static inline int event_decode(int events) {
	uint32_t flag = 0;
	if (events & WM_EVENT_READ) {
		flag |= EPOLLIN;
	}
	if (events & WM_EVENT_WRITE) {
		flag |= EPOLLOUT;
	}
	if (events & WM_EVENT_EPOLLEXCLUSIVE) { //避免惊群
		flag |= EPOLLEXCLUSIVE;
	}
	return flag;
}

/**
 * 删除事件
 */
bool _loop_del(int fd) {
	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}
	return true;
}

/**
 * 恢复协程&用于loop_wait回调
 */
bool loop_callback_coroutine_resume(wmSocket* socket, int event) {
	if (!socket) {
		wmError("Error has occurred: loop_callback_coroutine_resume . wmCoroutine is NULL");
		return false;
	}
	if (event == EPOLLIN && socket->read_co) {
		return wmCoroutine_resume(socket->read_co);
	} else if (event == EPOLLOUT && socket->write_co) {
		return wmCoroutine_resume(socket->write_co);
	}
	wmWarn("Error has occurred: loop_callback_coroutine_resume fail. will del event....");
	wmWorkerLoop_del(socket);
	return false;
}

bool loop_callback_coroutine_resume_and_del(wmSocket* socket, int event) {
	wmWorkerLoop_del(socket);
	bool b = loop_callback_coroutine_resume(socket, event);
	return b;
}

/**
 * 初始化loop需要的东西
 */
int loop_init() {
	if (!WorkerG.poll) {
		wmWorkerLoop_set_handler(WM_EVENT_READ, WM_LOOP_SEMI_AUTO, loop_callback_coroutine_resume);
		wmWorkerLoop_set_handler(WM_EVENT_WRITE, WM_LOOP_SEMI_AUTO, loop_callback_coroutine_resume);
		wmWorkerLoop_set_handler(WM_EVENT_READ, WM_LOOP_AUTO, loop_callback_coroutine_resume_and_del);
		wmWorkerLoop_set_handler(WM_EVENT_WRITE, WM_LOOP_AUTO, loop_callback_coroutine_resume_and_del);
		return init_wmPoll();
	}
	return 0;
}

/**
 *	设置event处理方式
 */
bool wmWorkerLoop_set_handler(int event, int type, loop_callback_func_t fn) {
	loop_callback_func_t *handlers;
	switch (event) {
	case WM_EVENT_READ:
		handlers = read_handler;
		break;
	case WM_EVENT_WRITE:
		handlers = write_handler;
		break;
	default:
		return false;
	}
	handlers[type] = fn;
	return true;
}

loop_callback_func_t loop_get_handler(int event, int type) {
	loop_callback_func_t *handlers;
	switch (event) {
	case EPOLLIN:
		handlers = read_handler;
		break;
	case EPOLLOUT:
		handlers = write_handler;
		break;
	default:
		return NULL;
	}
	if (handlers[type] != NULL) {
		return handlers[type];
	}
	return NULL;
}

bool wmWorkerLoop_add(wmSocket* socket, int event) {
	if (socket->events & event) { //如果socket里面有这个事件,那直接返回
		return true;
	}

	int LOOP_TYPE = EPOLL_CTL_MOD;
	if (socket->events == WM_EVENT_NULL) {
		LOOP_TYPE = EPOLL_CTL_ADD;
	}
	socket->events |= event;

	//初始化epoll
	loop_init();
	struct epoll_event *ev;
	ev = WorkerG.poll->event;
	//转换epoll能看懂的事件类型
	ev->events = event_decode(socket->events);
	ev->data.ptr = socket;

	//注册到全局的epollfd上面。
	if (epoll_ctl(WorkerG.poll->epollfd, LOOP_TYPE, socket->fd, ev) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}
	return true;
}

/**
 * 减去一个事件
 */
bool wmWorkerLoop_remove(wmSocket* socket, int event) {
	//如果本来就没有,那直接返回
	if (socket->events == WM_EVENT_NULL) {
		return true;
	}
	if (!(socket->events & event)) { //如果socket里面没有这个事件
		return true;
	}
	socket->events &= (~event); //那么就减去这个事件
	//如果已经没有事件了，就删除监听
	if (socket->events == WM_EVENT_NULL) {
		return wmWorkerLoop_del(socket);
	}

	//初始化epoll
	loop_init();
	struct epoll_event *ev;
	ev = WorkerG.poll->event;
	//转换epoll能看懂的事件类型
	ev->events = event_decode(socket->events);
	ev->data.ptr = socket;

	//注册到全局的epollfd上面。
	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_MOD, socket->fd, ev) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}
	return true;
}

bool wmWorkerLoop_del(wmSocket* socket) {
	socket->events = WM_EVENT_NULL;
	return _loop_del(socket->fd);
}

/**
 * 主事件循环，不同于wm_event_wait
 */
void wmWorkerLoop_loop() {
	if (loop_init() < 0) {
		wmError("Need to call init_wmPoll() first.");
	}
	WorkerG.is_running = true;

	int n;
	long mic_time;
	loop_callback_func_t fn;
	while (WorkerG.is_running) {
		//毫秒级定时器，必须是1
		int timeout = 1;
		struct epoll_event *events;
		events = WorkerG.poll->events;
		n = epoll_wait(WorkerG.poll->epollfd, events, WorkerG.poll->ncap, timeout);
		//循环处理epoll请求
		for (int i = 0; i < n; i++) {
			wmSocket* socket = events[i].data.ptr;

			//read
			if (events[i].events & EPOLLIN) {
				fn = loop_get_handler(EPOLLIN, socket->loop_type);
				if (fn != NULL) {
					fn(socket, EPOLLIN);
				}
			}

			//write 如果是可写，那么就恢复协程
			if (events[i].events & EPOLLOUT) {
				fn = loop_get_handler(EPOLLOUT, socket->loop_type);
				if (fn != NULL) {
					fn(socket, EPOLLOUT);
				}
			}
		}
		//有定时器才更新
		if (WorkerG.timer.num > 0) {
			//获取毫秒
			wmGetMilliTime(&mic_time);
			wmTimerWheel_update(&WorkerG.timer, mic_time);
		}
	}
	wmWorkerLoop_stop();
}

void wmWorkerLoop_stop() {
	free_wmPoll();
}
