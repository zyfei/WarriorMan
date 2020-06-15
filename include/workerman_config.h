#ifndef WM_CONFIG_H_
#define WM_CONFIG_H_

#define PHP_WORKERMAN_VERSION "0.1.0"

enum wmSocket_type {
	WM_SOCK_TCP = 1, //
	WM_SOCK_UDP = 2, //
};

enum wmLoop_type {
	WM_LOOP_WORKER = 1, //
	WM_LOOP_CONNECTION = 2, //
	WM_LOOP_SIGAL = 3, //
};

enum wmChannel_opcode {
	CHANNEL_PUSH = 1, //
	CHANNEL_POP = 2,
};

/**
 * epoll各种监听事件
 */
enum wmEvent_type {
	WM_EVENT_NULL = 0, //
	WM_EVENT_READ = 1u << 9, //
	WM_EVENT_WRITE = 1u << 10, //
	WM_EVENT_ERROR = 1u << 11, //
	WM_EVENT_ONCE = 1u << 12, //只执行一次
	WM_EVENT_EPOLLEXCLUSIVE = 1u << 13, //防止惊群效应
};

/**
 * worker的状态
 */
enum wmWorker_status {
	WM_WORKER_STATUS_STARTING = 1, //
	WM_WORKER_STATUS_RUNNING = 2, //
	WM_WORKER_STATUS_SHUTDOWN = 4, //
	WM_WORKER_STATUS_RELOADING = 8, //
};

//worker_worker.h
#define WM_KILL_WORKER_TIMER_TIME 2000  //2000毫秒后杀死进程

/**
 * Connection的状态
 */
enum wmConnection_status {
	WM_CONNECTION_STATUS_INITIAL = 0, //没用上，作为客户端使用
	WM_CONNECTION_STATUS_CONNECTING = 1, //没用上，作为客户端使用
	WM_CONNECTION_STATUS_ESTABLISHED = 2, //刚建立
	WM_CONNECTION_STATUS_CLOSING = 4, //正在关闭
	WM_CONNECTION_STATUS_CLOSED = 8 //已关闭
};

//worker connection
#define WM_MAX_SEND_BUFFER_SIZE 102400 //默认应用层发送缓冲区大小  1M
#define WM_MAX_PACKAGE_SIZE 1024000   //每个连接能够接收的最大包包长 10M

//coroutine.h 默认的PHP栈页大小
#define DEFAULT_PHP_STACK_PAGE_SIZE       8192
#define PHP_CORO_TASK_SLOT ((int)((ZEND_MM_ALIGNED_SIZE(sizeof(wmCoroutine)) + ZEND_MM_ALIGNED_SIZE(sizeof(zval)) - 1) / ZEND_MM_ALIGNED_SIZE(sizeof(zval))))
#define DEFAULT_C_STACK_SIZE          (2 *1024 * 1024)

#define WM_MAXEVENTS            1024   //每次epoll可以返回的事件数量上限
#define WM_BUFFER_SIZE_BIG         65536 //默认一次从管道中读字节长度
#define WM_DEFAULT_BACKLOG	102400	//默认listen的时候backlog最大长度，也就是等待accept的队列最大长度

//file
#define WM_MAX_FILE_CONTENT        (64*1024*1024) //文件最大字节数

//worker
#define WM_ACCEPT_MAX_COUNT              12  //每次epoll通知可以accept，都会循环12次去accept，swoole设置的64，我们是多进程模型，不需要设置一次读取那么多

//array
#define WM_ARRAY_PAGE_MAX  1024 //wmArray默认的page数是多少，每一次扩展都会申请一页的内存

#define wm_malloc              emalloc
#define wm_free                efree
#define wm_calloc              ecalloc
#define wm_realloc             erealloc

#endif /* WM_CONFIG_H_ */
