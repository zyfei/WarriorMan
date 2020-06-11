#ifndef _WM_WORKER_LOOP_H
#define _WM_WORKER_LOOP_H

#include "base.h"
#include "coroutine.h"
#include "worker.h"
#include "array.h"
#include "socket.h"
#include "log.h"

/**
 * 保存触发信号之后的回调方法
 */
typedef void (*loop_sigal_func_t)(int);

void wmWorkerLoop_add(int fd, int events);
void wmWorkerLoop_add_sigal(int sigal, loop_sigal_func_t fn);
void wmWorkerLoop_update(int fd, int events);
void wmWorkerLoop_loop();
void wmWorkerLoop_stop();
void wmWorkerLoop_del(int fd);

#endif
