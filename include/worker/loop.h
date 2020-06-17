#ifndef _WM_WORKER_LOOP_H
#define _WM_WORKER_LOOP_H

#include "base.h"
#include "coroutine.h"
#include "array.h"
#include "socket.h"
#include "log.h"

/**
 * 保存触发信号之后的回调方法
 */
typedef void (*loop_func_t)(int);
typedef void (*loop_callback_func_t)(int, int);

void loop_callback_coroutine_resume(int fd, int coro_id);
#define WM_LOOP_RESUME loop_callback_coroutine_resume

bool wmWorkerLoop_set_handler(int event, int type, loop_callback_func_t fn);
void wmWorkerLoop_add_sigal(int sigal, loop_func_t fn);
void wmWorkerLoop_add(int fd, int events, int fdtype);
void wmWorkerLoop_update(int fd, int events, int fdtype);
void wmWorkerLoop_loop();
void wmWorkerLoop_stop();
void wmWorkerLoop_del(int fd);

#endif
