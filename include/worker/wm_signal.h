#ifndef _WM_SIGNAL_H
#define _WM_SIGNAL_H

#include "loop.h"

/**
 * 保存触发信号之后的回调方法
 */
typedef void (*signal_func_t)(int);
void wmSignal_add(int sigal, signal_func_t fn);

#endif
