#ifndef _WM_BASH_H
#define _WM_BASH_H

//只引入自己做的基础库
#include "header.h"
#include "workerman_config.h"
#include "hashmap.h"
#include "timer.h"
#include "socket.h"
#include "stack.h"
#include "error.h"
#include "log.h"
#include "wm_string.h"
#include "coroutine.h"
#include "queue.h"

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

inline zval* wm_malloc_zval() {
	return (zval *) emalloc(sizeof(zval));
}

inline zval* wm_zval_dup(zval *val) {
	zval *dup = wm_malloc_zval();
	memcpy(dup, val, sizeof(zval));
	return dup;
}

//初始化base相关
void workerman_base_init();
//定义协程注册方法
void workerman_coroutine_init();
//协程类静态create方法，封装为方法
long worker_go();
//socket注册方法
void workerman_socket_init();
//server注册方法
void workerman_server_init();
//channel注册方法
void workerman_channel_init();

//定义的一些结构体
typedef struct {
	int epollfd; //创建的epollfd。
	int ncap; //events数组的大小。
	int event_num; // 事件的数量
	struct epoll_event *events; //是用来保存epoll返回的事件。
} wmPoll_t;

typedef struct {
	bool is_running;
	wmPoll_t *poll;
	timerwheel_t timer; //核心定时器
} wmGlobal_t;

//初始化wmPoll_t
int init_wmPoll();
//释放
int free_wmPoll();

int wm_event_init();
int wm_event_wait();
int wm_event_free();

//定义的全局变量，在base.cc中
extern wmGlobal_t WorkerG;

#endif	/* _WM_BASH_H */
