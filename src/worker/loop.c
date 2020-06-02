#include "loop.h"

//把我们自己的events转换成epoll的
static inline int event_decode(int events) {
	uint32_t flag = 0;

	if (events & WM_EVENT_READ) {
		flag |= EPOLLIN;
	}
	if (events & WM_EVENT_WRITE) {
		flag |= EPOLLOUT;
	}
	if (events & WM_EVENT_ONCE) {
		flag |= EPOLLONESHOT;
	}
	if (events & WM_EVENT_ERROR) {
		flag |= (EPOLLRDHUP | EPOLLHUP | EPOLLERR);
	}
	return flag;
}

void wmWorkerLoop_add(int fd, int events) {
	//初始化epoll
	if (!WorkerG.poll) {
		init_wmPoll();
	}
	//没有事件
	if (events == WM_EVENT_NULL) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return;
	}

	int co_id = 0;
	wmCoroutine* co = wmCoroutine_get_current();
	if (co != NULL) {
		co_id = co->cid;
	}

	struct epoll_event *ev;
	ev = WorkerG.poll->events;
	//转换epoll能看懂的事件类型
	ev->events = event_decode(events) | EPOLLEXCLUSIVE;
	ev->data.u64 = touint64(fd, co_id);
	//注册到全局的epollfd上面。
	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_ADD, fd, ev) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return;
	}
}

void wmWorkerLoop_update(int fd, int events) {
	//初始化epoll
	if (!WorkerG.poll) {
		init_wmPoll();
	}
	//没有事件
	if (events == WM_EVENT_NULL) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return;
	}

	int co_id = 0;
	wmCoroutine* co = wmCoroutine_get_current();
	if (co != NULL) {
		co_id = co->cid;
	}

	struct epoll_event *ev;
	ev = WorkerG.poll->events;
	//转换epoll能看懂的事件类型
	ev->events = event_decode(events | EPOLLEXCLUSIVE);
	ev->data.u64 = touint64(fd, co_id);

	//注册到全局的epollfd上面。
	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_MOD, fd, ev) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return;
	}
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
	wm_event_init();
	if (!WorkerG.poll) {
		wmError("Need to call wm_event_init first.");
	}

	long mic_time;
	//这里应该改成死循环了
	while (WorkerG.is_running) {
		int n;
		//毫秒级定时器，必须是1
		int timeout = 1;
		struct epoll_event *events;
		events = WorkerG.poll->events;
		n = epoll_wait(WorkerG.poll->epollfd, events, WorkerG.poll->ncap,
				timeout);
		//循环处理epoll请求
		for (int i = 0; i < n; i++) {
			int fd, coro_id;
			fromuint64(events[i].data.u64, &fd, &coro_id);
			wmWorker* worker = wmWorker_find_by_fd(fd);
			//这个是worker Accept的
			if (worker) {
				_wmWorker_acceptConnection(worker);
				continue;
			}

			//read
			if (events[i].events & EPOLLIN) {
				_wmConnection_read_callback(fd);
			}
			//write
			if (events[i].events & EPOLLOUT) {
				_wmConnection_write_callback(fd, coro_id);
			}
			//error  以后完善
			if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
				//ignore ERR and HUP, because event is already processed at IN and OUT handler.
				if ((events[i].events & EPOLLIN)
						|| (events[i].events & EPOLLOUT)) {
					continue;
				}
				wmWarn("Error has occurred: (errno %d) %s", errno,
						strerror(errno));
			}
		}
		//有定时器才更新
		if (WorkerG.timer.num > 0) {
			//获取毫秒
			wmGetMilliTime(&mic_time);
			wmTimerWheel_update(&WorkerG.timer, mic_time);
		}
	}
}

void wmWorkerLoop_stop() {
	wm_event_free();
}
