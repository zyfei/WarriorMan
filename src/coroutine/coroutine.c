#include "coroutine.h"

static long last_cid = 0; //自增id
static swHashMap *coroutines = NULL; //保存所有协程
static swHashMap *user_yield_coros = NULL; //被yield的协程

static wmCoroutine main_task = { 0 }; //主协程
static wmCoroutine* current_task = NULL; //当前协程

long run(wmCoroutine* task);
void main_func(void *arg);
void vm_stack_init();
void save_vm_stack(wmCoroutine *task);
void close_coro(wmCoroutine *task);
wmCoroutine* get_origin_task(wmCoroutine *task);
wmCoroutine* get_task();
void restore_vm_stack(wmCoroutine *task);

/**
 * 创建一个协程
 */
long wmCoroutine_create(zend_fcall_info_cache *fci_cache, uint32_t argc,
		zval *argv) {
	if (!coroutines) {
		coroutines = swHashMap_new(NULL); //保存所有协程
	}
	if (!user_yield_coros) {
		user_yield_coros = swHashMap_new(NULL);
	}

	php_coro_args php_coro_args;
	php_coro_args.fci_cache = fci_cache;
	php_coro_args.argv = argv;
	php_coro_args.argc = argc;

	//保存current_task或者main_task协程信息 。如果current_task等于空，代表是主协程
	//也就是把上一个协程堆栈，保存到main_task或者current_task中
	save_vm_stack(get_task());

	//创建协程,注意 这个时候当前的task还没有php协程栈，是在main_func中初始化的
	wmCoroutine* task = (wmCoroutine *) wm_malloc(sizeof(wmCoroutine));
	bzero(task, sizeof(wmCoroutine));

	size_t stack_size = DEFAULT_C_STACK_SIZE;
	wmContext_init(&task->ctx, stack_size, main_func, ((void*) &php_coro_args));

	task->cid = ++last_cid;
	swHashMap_add_int(coroutines, task->cid, task);

	return run(task);
}

long run(wmCoroutine* task) {
	long cid = task->cid;
	//起源协程 = 记录的上一个协程
	task->origin = current_task;
	current_task = task;

	//切换到这个堆栈来工作,在这里面切换了C栈，并且在回调中申请了php协程栈
	wmContext_swap_in(&task->ctx);
	//下面有可能执行完毕，也有可能程序自己yield了

	//判断一下是否执行完毕了
	if (task->ctx.end_) {
		//如果不相等，说明已经创建了其他的协程
		assert(current_task == task);

		//如果没创建其他协程
		current_task = task->origin; //重新设置当前协程为，task的起源协程
		close_coro(task); //关闭当前协程
	}
	return cid;
}

/**
 * 创建协程,执行协程主方法
 */
void main_func(void *arg) {

	//把一些核心内容提取出来，存放在其他变量里面。
	int i;
	php_coro_args *php_arg = (php_coro_args *) arg;
	zend_fcall_info_cache fci_cache = *php_arg->fci_cache;
	zend_function *func = fci_cache.function_handler;
	zval *argv = php_arg->argv;
	int argc = php_arg->argc;
	zend_execute_data *call;
	zval _retval, *retval = &_retval;

	//初始化一个新php栈，并且放入EG
	vm_stack_init();
	call = (zend_execute_data *) (EG(vm_stack_top));

	//因为php中内存分配是有对齐的，所以取真实地址
	EG(vm_stack_top) = (zval *) ((char *) call
			+ PHP_CORO_TASK_SLOT * sizeof(zval));

	//函数分配一块用于当前作用域的内存空间，返回结果是zend_execute_data的起始位置。
	call = zend_vm_stack_push_call_frame(
			ZEND_CALL_TOP_FUNCTION | ZEND_CALL_ALLOCATED, func, argc,
			fci_cache.called_scope, fci_cache.object);

	for (i = 0; i < argc; ++i) {
		zval *param;
		zval *arg = &argv[i];
		param = ZEND_CALL_ARG(call, i + 1);
		ZVAL_COPY(param, arg);
	}

	call->symbol_table = NULL;

	//执行区域设置为call
	EG(current_execute_data) = call;

	//当前协程
	wmCoroutine* _task = current_task;
	//保存php栈信息
	save_vm_stack(_task);

	_task->defer_tasks = NULL;

	if (func->type == ZEND_USER_FUNCTION) {
		ZVAL_UNDEF(retval);
		EG(current_execute_data) = NULL;
		zend_init_func_execute_data(call, &func->op_array, retval);
		zend_execute_ex(EG(current_execute_data));
	}

	wmStack *defer_tasks = _task->defer_tasks;

	if (defer_tasks) {
		php_fci_fcc *defer_fci_fcc;
		zval result;
		while (wmStack_len(defer_tasks) > 0) {
			defer_fci_fcc = (php_fci_fcc*) wmStack_pop(defer_tasks);

			defer_fci_fcc->fci.retval = &result;

			if (zend_call_function(&defer_fci_fcc->fci, &defer_fci_fcc->fcc)
					!= SUCCESS) {
				php_error_docref(NULL, E_WARNING, "defer execute error");
				return;
			}
			efree(defer_fci_fcc);
		}
		wmStack_destroy(defer_tasks);
		_task->defer_tasks = NULL;
	}
	//释放
	zval_ptr_dtor(retval);
}

/**
 * 获取当前协程任务
 */
wmCoroutine* get_task() {
	//如果当前协程不为空，就取当前协程
	if (!current_task) {
		current_task = &main_task;
	}
	return current_task; //否则取主协程
}

/**
 * 初始化一个协程栈,并且替换当前的协程
 */
void vm_stack_init() {
	uint32_t size = DEFAULT_PHP_STACK_PAGE_SIZE;
	zend_vm_stack page = (zend_vm_stack) emalloc(size);

	//page->top的作用是指向目前的栈顶，这个top会随着栈里面的数据而不断的变化。压栈，top往靠近end的方向移动个；出栈，top往远离end的方向移动。
	page->top = ZEND_VM_STACK_ELEMENTS(page);
	//page->end的作用就是用来标识PHP栈的边界，以防'栈溢出'。这个page->end可以作为是否要扩展PHP栈的依据。
	page->end = (zval*) ((char*) page + size);
	page->prev = NULL;

	//修改现在的PHP栈，让它指向我们申请出来的新的PHP栈空间。
	EG(vm_stack) = page;
	EG(vm_stack)->top++;
	EG(vm_stack_top) = EG(vm_stack)->top;
	EG(vm_stack_end) = EG(vm_stack)->end;
	EG(vm_stack_page_size) = size;
}

/**
 * 读取当前PHP运行栈，存入task中
 */
void save_vm_stack(wmCoroutine *task) {
	task->vm_stack_top = EG(vm_stack_top);
	task->vm_stack_end = EG(vm_stack_end);
	task->vm_stack = EG(vm_stack);
	task->vm_stack_page_size = EG(vm_stack_page_size);
	task->execute_data = EG(current_execute_data);
}

/**
 * 切换协程
 */
void wmCoroutine_yield() {
	wmCoroutine* task = wmCoroutine_get_current();
	assert(current_task == task); //是否具备切换资格
	//放入协程切换队列
	swHashMap_add_int(user_yield_coros, task->cid, task);

	wmCoroutine *origin_task = task->origin;

	save_vm_stack((wmCoroutine *) task);
	restore_vm_stack((wmCoroutine *) origin_task);

	current_task = origin_task;

	//让出CPU控制权
	wmContext_swap_out(&task->ctx);
}

/**
 * 恢复协程
 */
bool wmCoroutine_resume(wmCoroutine *task) {
	assert(current_task != task);

	//判断是否之前yield过
	wmCoroutine* yield_co = (wmCoroutine*) swHashMap_find_int(user_yield_coros, task->cid);
	if (yield_co == NULL) {
		return false;
	}
	swHashMap_del_int(user_yield_coros, task->cid);

	wmCoroutine *_current_task = get_task();
	//保存当前的协程
	save_vm_stack(_current_task);
	//恢复指定的task
	restore_vm_stack(task);

	task->origin = current_task;
	current_task = task;

	wmContext_swap_in(&task->ctx);
	if (task->ctx.end_) {
		//如果不相等，说明已经创建了其他的协程
		assert(current_task == task);

		//如果没创建其他协程
		current_task = task->origin; //重新设置当前协程为，task的起源协程
		close_coro(task); //关闭当前协程
	}
	return true;
}

void close_coro(wmCoroutine *task) {
	//在hash表中删除
	swHashMap_del_int(coroutines, task->cid);

	//yield表中删除
	swHashMap_del_int(user_yield_coros, task->cid);

	//获取他的起源
	wmCoroutine *origin_task = get_origin_task(task);
	//销毁ctx
	wmContext_destroy(&task->ctx);

	vm_stack_destroy();
	restore_vm_stack(origin_task);

	//销毁自己
	wm_free(task);
	task = NULL;
}

//获取起源协程
wmCoroutine* get_origin_task(wmCoroutine *task) {
	wmCoroutine *_origin = task->origin;
	return _origin ? _origin : &main_task;
}

//加载一个php运行栈
void restore_vm_stack(wmCoroutine *task) {
	EG(vm_stack_top) = task->vm_stack_top;
	EG(vm_stack_end) = task->vm_stack_end;
	EG(vm_stack) = task->vm_stack;
	EG(vm_stack_page_size) = task->vm_stack_page_size;
	EG(current_execute_data) = task->execute_data;
}

//清空整个php允许栈，我们不需要保存，都在自己task内保存
void vm_stack_destroy() {
	zend_vm_stack stack = EG(vm_stack);

	while (stack != NULL) {
		zend_vm_stack p = stack->prev;
		//内存叶读取出问题，好像重复释放了
		efree(stack);
		stack = p;
	}
}

wmCoroutine* wmCoroutine_get_by_cid(long _cid) {
	wmCoroutine* _co = (wmCoroutine*) swHashMap_find_int(coroutines, _cid);
	return _co;
}

void wmCoroutine_defer(php_fci_fcc *defer_fci_fcc) {
	if (current_task->defer_tasks == NULL) {
		current_task->defer_tasks = wmStack_create();
	}
	wmStack_push(current_task->defer_tasks, defer_fci_fcc);
}

//返回当前协程任务
wmCoroutine* wmCoroutine_get_current() {
	return current_task;
}
