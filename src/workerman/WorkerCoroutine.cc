#include "coroutine.h"

using namespace std;
using workerman::WorkerCoroutine;
using workerman::Coroutine;

//主协程
php_coro_task WorkerCoroutine::main_task = { 0 };

void WorkerCoroutine::init() {
	Coroutine::set_on_yield(on_yield);
	Coroutine::set_on_resume(on_resume);
	Coroutine::set_on_close(on_close);
}

/**
 * 创建协程
 */
long WorkerCoroutine::create(zend_fcall_info_cache *fci_cache, uint32_t argc,
		zval *argv) {
	php_coro_args php_coro_args;
	php_coro_args.fci_cache = fci_cache;
	php_coro_args.argv = argv;
	php_coro_args.argc = argc;
	save_task(get_task());
	return Coroutine::create(create_func, (void*) &php_coro_args);
}

/**
 * 保存协程任务信息
 */
void WorkerCoroutine::save_task(php_coro_task *task) {
	save_vm_stack(task);
}

/**
 * 获取当前协程任务
 */
php_coro_task* WorkerCoroutine::get_task() {
	php_coro_task *task = (php_coro_task*) Coroutine::get_current_task();
	return task ? task : &main_task;
}

/**
 * 保存协程堆栈
 */
void WorkerCoroutine::save_vm_stack(php_coro_task *task) {
	task->vm_stack_top = EG(vm_stack_top);
	task->vm_stack_end = EG(vm_stack_end);
	task->vm_stack = EG(vm_stack);
	task->vm_stack_page_size = EG(vm_stack_page_size);
	task->execute_data = EG(current_execute_data);
}

/**
 * 辅助创建协程
 */
void WorkerCoroutine::create_func(void *arg) {

	//把一些核心内容提取出来，存放在其他变量里面。
	int i;
	php_coro_args *php_arg = (php_coro_args *) arg;
	zend_fcall_info_cache fci_cache = *php_arg->fci_cache;
	zend_function *func = fci_cache.function_handler;
	zval *argv = php_arg->argv;
	int argc = php_arg->argc;
	php_coro_task *task;
	zend_execute_data *call;
	zval _retval, *retval = &_retval;

	//初始化一个新php栈，并且放入EG
	vm_stack_init(); // get a new php stack
	call = (zend_execute_data *) (EG(vm_stack_top));
	task = (php_coro_task *) EG(vm_stack_top);
	//往后挪了点位置
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

	//保存php栈信息
	save_vm_stack(task);

	task->co = Coroutine::get_current();
	task->co->set_task((void *) task);
	task->defer_tasks = nullptr;

	if (func->type == ZEND_USER_FUNCTION) {
		ZVAL_UNDEF(retval);
		EG(current_execute_data) = NULL;
		zend_init_func_execute_data(call, &func->op_array, retval);
		zend_execute_ex(EG(current_execute_data));
	}

	task = get_task();
	wmStack *defer_tasks = task->defer_tasks;

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
		task->defer_tasks = nullptr;
	}
	//释放
	zval_ptr_dtor(retval);
}

/**
 * 初始化一个协程栈,并且替换当前的协程
 */
void WorkerCoroutine::vm_stack_init(void) {
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

void WorkerCoroutine::on_yield(void *arg) {
	php_coro_task *task = (php_coro_task *) arg;
	php_coro_task *origin_task = get_origin_task(task);
	save_task(task);
	restore_task(origin_task);
}

void WorkerCoroutine::on_resume(void *arg) {
	php_coro_task *task = (php_coro_task *) arg;
	php_coro_task *current_task = get_task();
	save_task(current_task);
	restore_task(task);
}

void WorkerCoroutine::on_close(void *arg) {
	php_coro_task *task = (php_coro_task *) arg;
	php_coro_task *origin_task = get_origin_task(task);
	zend_vm_stack stack = EG(vm_stack);
	efree(stack);
	restore_task(origin_task);
}

/**
 * load PHP stack
 */
void WorkerCoroutine::restore_task(php_coro_task *task) {
	restore_vm_stack(task);
}

/**
 * load PHP stack
 */
inline void WorkerCoroutine::restore_vm_stack(php_coro_task *task) {
	EG(vm_stack_top) = task->vm_stack_top;
	EG(vm_stack_end) = task->vm_stack_end;
	EG(vm_stack) = task->vm_stack;
	EG(vm_stack_page_size) = task->vm_stack_page_size;
	EG(current_execute_data) = task->execute_data;
}

void WorkerCoroutine::defer(php_fci_fcc *defer_fci_fcc) {
	php_coro_task *task = (php_coro_task *) get_task();
	if (task->defer_tasks == nullptr) {
		task->defer_tasks = wmStack_create();
	}
	wmStack_push(task->defer_tasks, defer_fci_fcc);
}

/**
 * sleep的回调函数
 */
void WorkerCoroutine::sleep(void *co) {
	((Coroutine *) co)->resume();
}

//调度器
int WorkerCoroutine::scheduler() {
	size_t size;
	//uv_loop_t *loop = uv_default_loop();
	WorkerG.poll.epollfd = epoll_create(512); //创建一个epollfd，然后保存在全局变量
	WorkerG.poll.ncap = WM_MAXEVENTS; //有16个event
	size = sizeof(struct epoll_event) * WorkerG.poll.ncap;
	WorkerG.poll.events = (struct epoll_event *) malloc(size);
	memset(WorkerG.poll.events, 0, size);

	long mic_time;
	while (WorkerG.timer.num > 0) {
		epoll_wait(WorkerG.poll.epollfd, WorkerG.poll.events, WorkerG.poll.ncap,
				1);
		//一秒过去了
		wmGetMilliTime(&mic_time);
		timerwheel_update(&WorkerG.timer, mic_time);
	}

//	StudyG.poll.epollfd = epoll_create(256);
//	StudyG.poll.ncap = 16;
//	size = sizeof(struct epoll_event) * StudyG.poll.ncap;
//	StudyG.poll.events = (struct epoll_event *) malloc(size);
//	memset(StudyG.poll.events, 0, size);
//
//	while (loop->stop_flag == 0) {
//		timeout = uv__next_timeout(loop);
//		epoll_wait(StudyG.poll.epollfd, StudyG.poll.events, StudyG.poll.ncap,
//				timeout);
//
//		loop->time = uv__hrtime(UV_CLOCK_FAST) / 1000000;
//		uv__run_timers(loop);
//
//		if (uv__next_timeout(loop) < 0) {
//			uv_stop(loop);
//		}
//	}

	return 0;
}
