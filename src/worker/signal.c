#include "wm_signal.h"

/**
 * 创建的信号通道
 * 载epoll版本信号
 * [0] 是读
 * [1] 是写
 */
static int signal_fd[2] = { 0, 0 };
static loop_func_t signal_handler_array[64];
static char signals[1024];

/**
 * 一旦出现信号，写入signal_fd中
 */
void sig_handler(int signalno) {
	//保留原来的errno，在函数最后回复，以保证函数的可重入性
	int save_errno = errno;
	int msg = signalno;

	//将信号值写入管代，通知主循环。
	//向1里面写，从0里面读
	if (wm_socket_send(signal_fd[1], (char*) &msg, 1, 0) <= 0) {
		php_printf("The message sent to the server failed\n");
	}
	errno = save_errno;
}

/**
 * 读信号，并且处理信号
 */
void sig_callback(int fd, int coro_id) {
	int recv_ret_value;
	do {
		recv_ret_value = wm_socket_recv(signal_fd[0], signals, sizeof(signals), 0);
	} while (recv_ret_value < 0 && errno == EAGAIN);

	if (recv_ret_value <= 0) { //信号fd被关闭了
		wmError("signal_fd be closed");
	}
	//每个信号值占1字节，所以按字节来逐个接收信号
	for (int i = 0; i < recv_ret_value; ++i) {
		int signal = signals[i];
		if (signal_handler_array[signal] != 0) {
			signal_handler_array[signal](signals[i]);
		}
	}
}

void wmSignal_add(int signal, signal_func_t fn) {
	if (signal_fd[0] == 0) {
		//创建一对互相连接的，全双工管道
		if (socketpair(PF_UNIX, SOCK_STREAM, 0, signal_fd) == -1) {
			wmError("socketpair");
		}
		/* sockpair函数创建的管道是全双工的，不区分读写端
		 * 此处我们假设pipe_fd[1]为写端，非阻塞
		 * pipe_fd[0]为读端
		 */
		wm_socket_set_nonblock(signal_fd[0]);
		wm_socket_set_nonblock(signal_fd[1]);

		//添加信号的读监听配置
		wmWorkerLoop_set_handler(WM_EVENT_READ, WM_LOOP_SIGAL, sig_callback);

		//最后加入loop
		wmWorkerLoop_add(signal_fd[0], WM_EVENT_READ, WM_LOOP_SIGAL);
	}

	struct sigaction act;
	bzero(&act, sizeof(act));
	act.sa_handler = sig_handler; //设置信号处理函数
	sigfillset(&act.sa_mask); //初始化信号屏蔽集
	act.sa_flags |= SA_RESTART; //由此信号中断的系统调用自动重启动

	//初始化信号处理函数
	if (sigaction(signal, &act, NULL) == -1) {
		php_printf("capture signal,but to deal with failure\n");
		return;
	}
	//加入信号数组中
	signal_handler_array[signal] = fn;
}
