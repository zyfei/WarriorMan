#ifndef WM_COROUTINE_H
#define WM_COROUTINE_H

#include "bash.h"
#include "context.h"

//defer用到
typedef struct {
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} php_fci_fcc;

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
	_Coroutine *origin; //起源协程，记录哪个协程，创建的这个协程
	wmStack *defer_tasks; //所有的defer
} wmCoroutine;

long wmCoroutine_create(zend_fcall_info_cache *fci_cache, uint32_t argc,
		zval *argv);
wmCoroutine* wmCoroutine_get_by_cid(long _cid);
void wmCoroutine_yield(wmCoroutine *task);
void wmCoroutine_resume(wmCoroutine *task);
void vm_stack_destroy();
void wmCoroutine_defer(php_fci_fcc *defer_fci_fcc);
wmCoroutine* wmCoroutine_get_current();

extern swHashMap *coroutines;

#endif	/* WM_COROUTINE_H */
