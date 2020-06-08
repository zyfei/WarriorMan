#ifndef WM_CONFIG_H_
#define WM_CONFIG_H_

enum wmEvent_type {
	WM_EVENT_NULL = 0, //
	WM_EVENT_READ = 1u << 9, //
	WM_EVENT_WRITE = 1u << 10, //
	WM_EVENT_ERROR = 1u << 11, //
	WM_EVENT_ONCE = 1u << 12, //只执行一次
	WM_EVENT_EPOLLEXCLUSIVE = 1u << 13, //防止惊群效应
};

enum wmWorker_status {
	WM_WORKER_STATUS_STARTING = 1, //
	WM_WORKER_STATUS_RUNNING = 2, //
	WM_WORKER_STATUS_SHUTDOWN = 4, //
	WM_WORKER_STATUS_RELOADING = 8, //
};

enum wmConnection_status {
	WM_CONNECTION_STATUS_INITIAL = 0, //没用上，作为客户端使用
	WM_CONNECTION_STATUS_CONNECTING = 1, //没用上，作为客户端使用
	WM_CONNECTION_STATUS_ESTABLISHED = 2, //
	WM_CONNECTION_STATUS_CLOSING = 4, //
	WM_CONNECTION_STATUS_CLOSED = 8 //
};

//coroutine.h 默认的PHP栈页大小
#define DEFAULT_PHP_STACK_PAGE_SIZE       8192
#define PHP_CORO_TASK_SLOT ((int)((ZEND_MM_ALIGNED_SIZE(sizeof(wmCoroutine)) + ZEND_MM_ALIGNED_SIZE(sizeof(zval)) - 1) / ZEND_MM_ALIGNED_SIZE(sizeof(zval))))
#define DEFAULT_C_STACK_SIZE          (2 *1024 * 1024)

#define WM_MAXEVENTS            1024
#define WM_BUFFER_SIZE_BIG         65536
#define WM_DEFAULT_BACKLOG	102400

//worker_worker.h
#define WM_STATUS_STARTING 1
#define WM_STATUS_RUNNING 2
#define WM_STATUS_SHUTDOWN 4
#define WM_STATUS_RELOADING 8

//worker connection
#define WM_MAX_SEND_BUFFER_SIZE 102400 //默认应用层发送缓冲区大小  1M
#define WM_MAX_PACKAGE_SIZE 1024000   //每个连接能够接收的最大包包长 10M

//file
#define WM_MAX_FILE_CONTENT        (64*1024*1024)

//worker
#define WM_ACCEPT_MAX_COUNT              12  //swoole设置的64，我们是多进程模型，不需要设置一次读取那么多

#define wm_malloc              malloc
#define wm_free                free
#define wm_calloc              calloc
#define wm_realloc             realloc

#endif /* WM_CONFIG_H_ */
