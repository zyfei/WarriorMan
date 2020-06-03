#ifndef _WM_WORKER_LOOP_H
#define _WM_WORKER_LOOP_H

/**
 * worker的头文件咯
 */
#include "base.h"
#include "coroutine.h"
#include "worker.h"

//typedef void (*loop_func_t)(void*);

void wmWorkerLoop_add(int fd, int events);
void wmWorkerLoop_update(int fd, int events);
void wmWorkerLoop_loop();
void wmWorkerLoop_stop();
void wmWorkerLoop_del(int fd);

#endif
