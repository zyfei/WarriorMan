#include "loop.h"

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
			loop_sigal_func_t fn = wmArray_find(signal_handler_array, signals[i]);
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

void wmWorkerLoop_add_sigal(int sigal, loop_sigal_func_t fn) {
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
		wmWorkerLoop_add(signal_fd[0], WM_EVENT_READ);
	}

	if (signal_handler_array == NULL) {
		signal_handler_array = wmArray_new(64, sizeof(loop_sigal_func_t));
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
	ev->events = event_decode(events);
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
	ev->events = event_decode(events);
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
	if (init_wmPoll() < 0) {
		wmError("Need to call init_wmPoll() first.");
	}
	WorkerG.is_running = true;

	int n;
	long mic_time;
	//这里应该改成死循环了
	while (WorkerG.is_running) {
		//毫秒级定时器，必须是1
		int timeout = 1000;
		struct epoll_event *events;
		events = WorkerG.poll->events;
		n = epoll_wait(WorkerG.poll->epollfd, events, WorkerG.poll->ncap, timeout);
		//循环处理epoll请求
		for (int i = 0; i < n; i++) {
			int fd, coro_id;
			fromuint64(events[i].data.u64, &fd, &coro_id);
			//先判断是不是信号fd,不用判断是不是读事件，因为只添加了读事件~

			if (fd == signal_fd[0]) {
				sig_callback(fd);
				continue;
			}

			//再判断是否是worker的
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
				if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLOUT)) {
					continue;
				}
				wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
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
