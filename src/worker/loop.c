#include "loop.h"

static int LOOP_TYPE = EPOLL_CTL_ADD;

static loop_callback_func_t read_handler[7];
static loop_callback_func_t write_handler[7];
static loop_callback_func_t error_handler[7];

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
 * 恢复协程&用于loop_wait回调
 */
void loop_callback_coroutine_resume(int fd, int coro_id) {
	wmCoroutine* co = wmCoroutine_get_by_cid(coro_id);
	if (co == NULL) {
		wmWarn("Error has occurred: loop_callback_coroutine_resume . wmCoroutine is NULL");
		return;
	}
	wmCoroutine_resume(co);
}
void loop_callback_coroutine_resume_and_del(int fd, int coro_id) {
	wmCoroutine* co = wmCoroutine_get_by_cid(coro_id);
	if (co == NULL) {
		wmWarn("Error has occurred: loop_callback_coroutine_resume . wmCoroutine is NULL");
		return;
	}
	wmWorkerLoop_del(fd); //删除监听
	wmCoroutine_resume(co);
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
		handlers = error_handler;
	}
	if (handlers[type] != NULL) {
		return handlers[type];
	}
	return NULL;
}

bool wmWorkerLoop_add(int fd, int events, int fdtype) {
	//初始化epoll
	if (!WorkerG.poll) {
		init_wmPoll();
	}

	//没有事件
	if (events == WM_EVENT_NULL) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}

	long coro_id = 0;
	wmCoroutine* co = wmCoroutine_get_current();
	if (co != NULL) {
		coro_id = co->cid;
	}

	struct epoll_event *ev;
	ev = WorkerG.poll->event;
	//转换epoll能看懂的事件类型
	ev->events = event_decode(events);

	//将三个数构建在一起前3位是type，然后后29位是fd，最后32位是co_id
	uint64_t ret = 0;
	ret |= ((uint64_t) fdtype) << 61;
	ret |= ((uint64_t) fd) << 32;
	ret |= ((uint64_t) coro_id);

	ev->data.u64 = ret;
	//注册到全局的epollfd上面。
	if (epoll_ctl(WorkerG.poll->epollfd, LOOP_TYPE, fd, ev) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}
	return true;
}

bool wmWorkerLoop_update(int fd, int events, int fdtype) {
	bool loop_return;
	LOOP_TYPE = EPOLL_CTL_MOD;
	loop_return = wmWorkerLoop_add(fd, events, fdtype);
	LOOP_TYPE = EPOLL_CTL_ADD;
	return loop_return;
}

void wmWorkerLoop_del(int fd) {
	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
	}
}

/**
 * 主事件循环，不同于wm_event_wait
 */
void wmWorkerLoop_loop() {
	if (init_wmPoll() < 0) {
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
			uint64_t v = events[i].data.u64;

			int fdtype = (int) (v >> 61);
			//成功的把头三位卡没了
			v = (v << 3) >> 3;
			int fd = (int) (v >> 32);

			int coro_id = (int) (v & 0xffffffff);

			php_printf("loop fd=%d\n",fd);

			//read
			if (events[i].events & EPOLLIN) {
				fn = loop_get_handler(EPOLLIN, fdtype);
				if (fn != NULL) {
					fn(fd, coro_id);
				}
			}

			//write 如果是可写，那么就恢复协程
			if (events[i].events & EPOLLOUT) {
				fn = loop_get_handler(EPOLLOUT, fdtype);
				if (fn != NULL) {
					fn(fd, coro_id);
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
