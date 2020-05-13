#ifndef WORKERMAN_HEADER_H_
#define WORKERMAN_HEADER_H_

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// include standard library
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <sys/timeb.h>


//c++才有这个 , 所以不用这个了，用自己写的stack.h实现
//#include <stack>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/epoll.h>

//同样使用自己实现的hashmap
//#include <unordered_map>

//自己写的扩展
#include "workerman_config.h"
#include "log.h"
#include "error.h"
#include "stack.h"
#include "hashmap.h"
#include "socket.h"
#include "timer.h"
#include "wm_string.h"
#include "coroutine_socket.h"

//int转字符串
int wm_itoa(char *buf, long value);
//随机
int wm_rand(int min, int max);

//定义一些全局方法
static inline zval *wm_zend_read_property(zend_class_entry *class_ptr,
		zval *obj, const char *s, int len, int silent) {
	zval rv;
	return zend_read_property(class_ptr, obj, s, len, silent, &rv);
}

//获取当前时间
static inline void wmGetTime(long *seconds, long *microseconds) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*seconds = tv.tv_sec;
	*microseconds = tv.tv_usec;
}

//只获取毫秒
static inline void wmGetMilliTime(long *microseconds) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*microseconds = tv.tv_sec * 1000 + (long) tv.tv_usec / 1000;
}

//只获取微妙
static inline void wmGetMicroTime(long *microseconds) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*microseconds = tv.tv_usec;
}

static inline uint64_t touint64(int fd, int id) {
	uint64_t ret = 0;
	ret |= ((uint64_t) fd) << 32;
	ret |= ((uint64_t) id);

	return ret;
}

static inline void fromuint64(uint64_t v, int *fd, int *id) {
	*fd = (int) (v >> 32);
	*id = (int) (v & 0xffffffff);
}

//初始化base相关
void workerman_base_init();
//定义协程注册方法
void workerman_coroutine_init();
//server注册方法
void workerman_server_init();
//协程类静态create方法，封装为方法
long worker_go();

//定义的一些结构体
typedef struct {
	int epollfd; //创建的epollfd。
	int ncap; //events数组的大小。
	struct epoll_event *events; //是用来保存epoll返回的事件。
} wmPoll_t;

typedef struct {
	wmPoll_t *poll;
	timerwheel_t timer; //核心定时器
} wmGlobal_t;

//初始化wmPoll_t
void init_wmPoll();
//释放
void free_wmPoll();

//定义的全局变量，在base.cc中
extern wmGlobal_t WorkerG;

#endif /* WORKERMAN_HEADER_H_ */
