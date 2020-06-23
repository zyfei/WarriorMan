#ifndef _WM_SIGNAL_H
#define _WM_SIGNAL_H

/**
 * 保存触发信号之后的回调方法
 */
typedef void (*signal_func_t)(int);
void wmSignal_add(int sigal, signal_func_t fn);
void wmSignal_wait();

#endif
