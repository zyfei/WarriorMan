#include "loop.h"

static swHashMap *_allEvents = NULL; //fd为key，保存着所有的事件
static wmStack* _allRecycleEvents = NULL; //将所有应该删除的event，都放在这里，给后面的连接用

wmWorkerLoopEvent* wmWorkerLoop_get_event() {
	if (_allRecycleEvents == NULL) {
		_allRecycleEvents = wmStack_create();
	}
	if (_allEvents == NULL) {
		_allEvents = swHashMap_new(NULL);
	}
	//先从_allRecycleEvents中弹
	if (wmStack_len(_allRecycleEvents) > 0) {
		wmWorkerLoopEvent * _event = (wmWorkerLoopEvent *) wmStack_pop(
				_allRecycleEvents);
		bzero(_event, sizeof(wmWorkerLoopEvent));
		return _event;
	}

	//如果没有就要NEW了
	//申请一个事件
	wmWorkerLoopEvent *_event = (wmWorkerLoopEvent *) wm_malloc(
			sizeof(wmWorkerLoopEvent));
	bzero(_event, sizeof(wmWorkerLoopEvent));
	return _event;
}

void wmWorkerLoop_add(int fd, int event, coroutine_func_t fn, void* data) {
	//初始化epoll
	if (!WorkerG.poll) {
		init_wmPoll();
	}

	wmWorkerLoopEvent *_event = wmWorkerLoop_get_event();
	_event->fd = fd;
	_event->fn = fn;
	_event->data = data;

	struct epoll_event *ev;

	ev = WorkerG.poll->events;

	//用来判断这个协程需要等待那种类型的事件，目前是支持WRITE和READ
	ev->events = (event == WM_EVENT_WRITE ? EPOLLOUT : EPOLLIN);

	ev->data.ptr = (void *) _event;

	//注册到全局的epollfd上面。
	epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_ADD, fd, ev);

	(WorkerG.poll->event_num)++; // 事件数量+1

	//切换协程
//	wmCoroutine_yield(co);
//
//	//程序到这里，说明已经执行完毕了。那么应该减去这个事件
//	if (epoll_ctl(WorkerG.poll->epollfd, EPOLL_CTL_DEL, socket->sockfd, NULL)
//			< 0) {
//		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
//		return false;
//	}
//
//	(WorkerG.poll->event_num)--; // 事件数量-1
//	return true;
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
			int fd;
			int id;
			struct epoll_event *p = &events[i];
			wmWorkerLoopEvent *_event = (wmWorkerLoopEvent *) p->data.ptr;
			//回调相关方法
			_event->fn(_event->data);
		}
		//有定时器才更新
		if (WorkerG.timer.num > 0) {
			//获取毫秒
			wmGetMilliTime(&mic_time);
			wmTimerWheel_update(&WorkerG.timer, mic_time);
		} else if (WorkerG.poll->event_num == 0) {
			WorkerG.is_running = false;
		}

	}

}

void wmWorkerLoop_free() {

	wm_event_free();
}
