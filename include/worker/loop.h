#ifndef _WM_WORKER_LOOP_H
#define _WM_WORKER_LOOP_H

/**
 * worker的头文件咯
 */
#include "bash.h"
#include "coroutine.h"

typedef struct {
	int fd;
	coroutine_func_t fn;
	void *data;
} wmWorkerLoopEvent;

typedef void (*worker_func_t)(void*);

wmWorkerLoopEvent* wmWorkerLoop_get_event();
void wmWorkerLoop_add(int fd, int event, coroutine_func_t fn, void* data);
void wmWorkerLoop_loop();
void wmWorkerLoop_free();

#endif
