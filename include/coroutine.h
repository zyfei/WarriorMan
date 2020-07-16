#ifndef WM_COROUTINE_H
#define WM_COROUTINE_H

#include "base.h"
#include "context.h"

//协程参数结构体
typedef struct {
	zend_fcall_info_cache *fci_cache;
	zval *argv;
	uint32_t argc;
} php_coro_args;

// 协程状态信息结构体
typedef struct _Coroutine {
	//格式不可以动
	zval *vm_stack_top; // 是协程栈栈顶
	zval *vm_stack_end; // 是协程栈栈底。
	zend_vm_stack vm_stack; // 是协程栈指针。
	size_t vm_stack_page_size; //是协程栈页大小。
	zend_execute_data *execute_data; // 是当前协程栈的栈帧。
	//格式不可以动

	//以下是coroutine结构
	wmContext ctx;
	long cid; //协程类ID
	struct _Coroutine *origin; //唤起协程，记录哪个协程，创建的这个协程
	wmStack *defer_tasks; //所有的defer

	coroutine_func_t _defer; //c语言级别defer
	void *_defer_data; //c语言级别defer
} wmCoroutine;

long wmCoroutine_create(zend_fcall_info_cache *fci_cache, uint32_t argc, zval *argv);
wmCoroutine* wmCoroutine_get_by_cid(long _cid);
void wmCoroutine_yield();
bool wmCoroutine_resume(wmCoroutine *task);
void vm_stack_destroy();
void wmCoroutine_defer(php_fci_fcc *defer_fci_fcc);
void wmCoroutine_sleep(double seconds);
void wmCoroutine_set_callback(long cid, coroutine_func_t _defer, void *_defer_data);
wmCoroutine* wmCoroutine_get_current();
void wmCoroutine_init();
void wmCoroutine_shutdown();
int wmCoroutine_getTotalNum(); //获取一共有多少个协程

#endif	/* WM_COROUTINE_H */
