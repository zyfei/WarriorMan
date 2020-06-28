#ifndef _WM_WORKER_LOOP_H
#define _WM_WORKER_LOOP_H

#include "base.h"
#include "wm_socket.h"

/**
 * 保存触发信号之后的回调方法
 */
typedef bool (*loop_callback_func_t)(wmSocket*, int);

bool wmWorkerLoop_set_handler(int event, int type, loop_callback_func_t fn);
loop_callback_func_t wmWorkerLoop_get_handler(int event, int type);
bool wmWorkerLoop_add(wmSocket* socket, int event);
bool wmWorkerLoop_remove(wmSocket* socket, int event);
void wmWorkerLoop_loop();
void wmWorkerLoop_stop();
bool wmWorkerLoop_del(wmSocket* socket);

#endif
