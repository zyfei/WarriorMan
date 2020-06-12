#include "loop.h"

static loop_callback_func_t read_handler[7];
static loop_callback_func_t write_handler[7];
static loop_callback_func_t error_handler[7];

/**
 * 创建的信号通道
 * 载epoll版本信号
 * [0] 是读
 * [1] 是写
 */
static int signal_fd[2] = { 0, 0 };
static wmArray* signal_handler_array = NULL;
static char signals[1024];

/**
 * 一旦出现信号，写入signal_fd中
 */
void sig_handler(int sigalno) {
	//保留原来的errno，在函数最后回复，以保证函数的可重入性
	int save_errno = errno;
	int msg = sigalno;

	//将信号值写入管代，通知主循环。
	//向1里面写，从0里面读
	if (wmSocket_send(signal_fd[1], (char*) &msg, 1, 0) <= 0) {
		php_printf("The message sent to the server failed\n");
	}
	errno = save_errno;
}

/**
 * 读信号，并且处理信号
 */
void sig_callback(int fd) {
	int recv_ret_value;
	do {
		recv_ret_value = wmSocket_recv(signal_fd[0], signals, sizeof(signals), 0);
	} while (recv_ret_value < 0 && errno == EAGAIN);

	if (recv_ret_value == 0) { //信号fd被关闭了
		wmError("signal_fd be closed");
		exit(1);
	}
	if (recv_ret_value < 0) { //信号fd被关闭了
		wmWarn("signal_fd read error");
	} else {
		//每个信号值占1字节，所以按字节来逐个接收信号
		for (int i = 0; i < recv_ret_value; ++i) {
			loop_func_t fn = wmArray_find(signal_handler_array, signals[i]);
			if (fn) {
				fn(signals[i]);
			}
		}
	}
}

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
	if (events & WM_EVENT_EPOLLEXCLUSIVE) { //避免惊群
		flag |= EPOLLEXCLUSIVE;
	}
	if (events & WM_EVENT_ERROR) {
		flag |= (EPOLLRDHUP | EPOLLHUP | EPOLLERR);
	}
	return flag;
}

void wmWorkerLoop_add_sigal(int sigal, loop_func_t fn) {
	if (signal_fd[0] == 0) {
		//创建一对互相连接的，全双工管道
		if (socketpair(PF_UNIX, SOCK_STREAM, 0, signal_fd) == -1) {
			wmError("socketpair");
			exit(1);
		}
		/* sockpair函数创建的管道是全双工的，不区分读写端
		 * 此处我们假设pipe_fd[1]为写端，非阻塞
		 * pipe_fd[0]为读端
		 */
		wmSocket_set_nonblock(signal_fd[0]);
		wmSocket_set_nonblock(signal_fd[1]);

		//最后加入loop
		wmWorkerLoop_add(signal_fd[0], WM_EVENT_READ, WM_LOOP_SIGAL);
	}

	if (signal_handler_array == NULL) {
		signal_handler_array = wmArray_new(64, sizeof(loop_func_t));
		signal_handler_array->page_num = 64;
	}

	struct sigaction act;
	bzero(&act, sizeof(act));
	act.sa_handler = sig_handler; //设置信号处理函数
	sigfillset(&act.sa_mask); //初始化信号屏蔽集
	act.sa_flags |= SA_RESTART; //由此信号中断的系统调用自动重启动

	//初始化信号处理函数
	if (sigaction(sigal, &act, NULL) == -1) {
		php_printf("capture signal,but to deal with failure\n");
		return;
	}

	//加入信号数组中
	wmArray_set(signal_handler_array, sigal, fn);
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
	case WM_EVENT_ERROR:
		handlers = error_handler;
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

static int LOOP_TYPE = EPOLL_CTL_ADD;
void wmWorkerLoop_add(int fd, int events, int fdtype) {
	//初始化epoll
	if (!WorkerG.poll) {
		init_wmPoll();
	}

	//没有事件
	if (events == WM_EVENT_NULL) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return;
	}

	long coro_id = 0;
	wmCoroutine* co = wmCoroutine_get_current();
	if (co != NULL) {
		coro_id = co->cid;
	}

	struct epoll_event *ev;
	ev = WorkerG.poll->events;
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
		return;
	}
}

void wmWorkerLoop_update(int fd, int events, int fdtype) {
	LOOP_TYPE = EPOLL_CTL_MOD;
	wmWorkerLoop_add(fd, events, fdtype);
	LOOP_TYPE = EPOLL_CTL_ADD;
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
	int _c = 0; //是否可以处理这个epoll信号，不能处理就del他
	while (WorkerG.is_running) {
		//毫秒级定时器，必须是1
		int timeout = 1;
		struct epoll_event *events;
		events = WorkerG.poll->events;
		n = epoll_wait(WorkerG.poll->epollfd, events, WorkerG.poll->ncap, timeout);
		//循环处理epoll请求
		for (int i = 0; i < n; i++) {
			_c = 0;
			uint64_t v = events[i].data.u64;
			int fdtype = (int) (v >> 61);
			//成功的把头三位卡没了
			v = (v << 3) >> 3;
			int fd = (int) (v >> 32);
			int coro_id = (int) (v & 0xffffffff);

			if (fd == signal_fd[0]) {
				sig_callback(fd);
			}

			//再判断是否是worker的

			//read
			if (events[i].events & EPOLLIN) {
				fn = loop_get_handler(EPOLLIN, fdtype);
				if (fn != NULL) {
					_c = 1;
					fn(fd, coro_id);
				}
			}
			//write
			if (events[i].events & EPOLLOUT) {
				fn = loop_get_handler(EPOLLOUT, fdtype);
				if (fn != NULL) {
					_c = 1;
					fn(fd, coro_id);
				}
			}
			//error
			if ((events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) && _c == 0) {
				fn = loop_get_handler(0, fdtype);
				if (fn != NULL) {
					_c = 1;
					fn(fd, coro_id);
				}
				wmWarn("loop  Error has occurred: (errno %d) %s", errno, strerror(errno));
			}

			/**
			 * 没有人可以处理的fd，赶快删掉避免死循环
			 */
			if (_c == 0) {
				wmWorkerLoop_del(fd);
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
