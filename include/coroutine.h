#ifndef COROUTINE_H
#define COROUTINE_H

#include "header.h"
#include "asm_context.h"
#include "bash.h"

//默认的PHP栈页大小
#define DEFAULT_PHP_STACK_PAGE_SIZE       8192
#define PHP_CORO_TASK_SLOT ((int)((ZEND_MM_ALIGNED_SIZE(sizeof(php_coro_task)) + ZEND_MM_ALIGNED_SIZE(sizeof(zval)) - 1) / ZEND_MM_ALIGNED_SIZE(sizeof(zval))))
#define DEFAULT_C_STACK_SIZE          (2 *1024 * 1024)

typedef fcontext_t coroutine_context_t;
typedef void (*coroutine_func_t)(void*);
typedef void (*st_coro_on_swap_t)(void*);

namespace workerman {
class Context {
public:
	Context(size_t stack_size, coroutine_func_t fn, void* private_data);
	~Context();  // 这是析构函数声明
	//执行创建php协程，并且切换上下文
	static void context_func(void* arg); // coroutine entry function
	//让出上下文
	bool swap_out();
	//加载上下文ctx
	bool swap_in();
	inline bool is_end() {
		return end_;
	}
protected:
	coroutine_func_t fn_;
	uint32_t stack_size_;
	void *private_data_;
	char* stack_;
	//指向汇编
	coroutine_context_t ctx_;
	coroutine_context_t swap_ctx_;
	bool end_ = false;
};

class Coroutine {
public:
	//保存所有协程
	static swHashMap *coroutines;
	//创建协程
	static long create(coroutine_func_t fn, void* args = nullptr);
	//获取当前协程
	static void* get_current_task();

	/**
	 * 返回当前协程类
	 */
	static Coroutine* get_current();

	//通过id获取协程
	static Coroutine* get_by_cid(long _cid);

	//设置自身协程
	void set_task(void *_task);
	//获取自身协程
	void* get_task();

	static void set_on_yield(st_coro_on_swap_t func);
	static void set_on_resume(st_coro_on_swap_t func);
	static void set_on_close(st_coro_on_swap_t func);

	inline Coroutine* get_origin() {
		return origin;
	}
	inline long get_cid() {
		return cid;
	}
	void yield();
	void resume();
protected:
	//造方法
	Coroutine(coroutine_func_t fn, void *private_data) :
			ctx(stack_size, fn, private_data) {
		cid = ++last_cid;
		swHashMap_add_int(coroutines, cid, this);
	}
	long run() {
		long cid = this->cid;
		origin = current;
		current = this;
		ctx.swap_in();
		//如果这个ctx已经运行完毕

		//我觉得是在这里，两个同样的ctx
		//在这里执行很重要的出栈操作
		if (ctx.is_end()) {
			assert(current == this);
			on_close(task);
			current = origin;
			swHashMap_del_int(coroutines, cid);
			delete this;
		}
		return cid;
	}
	//临时保存Coroutine信息
	static Coroutine* current;
	//自增id
	static long last_cid;
	//栈大小
	static size_t stack_size;
	//临时保存自身协程信息
	void *task = nullptr;
	Context ctx;
	//协程类ID
	long cid;
	Coroutine *origin = nullptr;

	static st_coro_on_swap_t on_yield;
	static st_coro_on_swap_t on_resume;
	static st_coro_on_swap_t on_close; /* before close */
};
}

//defer用到
struct php_fci_fcc {
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

//协程参数结构体
struct php_coro_args {
	zend_fcall_info_cache *fci_cache;
	zval *argv;
	uint32_t argc;
};

// 协程状态信息结构体
struct php_coro_task {
	zval *vm_stack_top; // 是协程栈栈顶
	zval *vm_stack_end; // 是协程栈栈底。
	zend_vm_stack vm_stack; // 是协程栈指针。
	size_t vm_stack_page_size; //是协程栈页大小。
	zend_execute_data *execute_data; // 是当前协程栈的栈帧。
	workerman::Coroutine *co;
	wmStack *defer_tasks; //所有的defer
	//std::stack<php_fci_fcc *> *defer_tasks; //所有的defer
};

/**
 * 定义类
 */
namespace workerman {
/**
 * 协程工具类
 */
class WorkerCoroutine {
public:
	static void init();
	static long create(zend_fcall_info_cache *fci_cache, uint32_t argc,
			zval *argv);
	static inline php_coro_task* get_origin_task(php_coro_task *task) {
		Coroutine *co = task->co->get_origin();
		return co ? (php_coro_task *) co->get_task() : &main_task;
	}
	static void defer(php_fci_fcc *defer_fci_fcc);
	static void sleep(void * co);
	static int scheduler();	//调度器
protected:
	//主协程
	static php_coro_task main_task;
	//保存php协程任务
	static void save_task(php_coro_task *task);
	//保存协程堆栈
	static void save_vm_stack(php_coro_task *task);
	//获取当前协程任务
	static php_coro_task* get_task();
	//辅助创建协程
	static void create_func(void *arg);
	//初始化协程栈
	static void vm_stack_init(void);

	static void on_yield(void *arg);
	static void on_resume(void *arg);
	static void on_close(void *arg);

	static inline void restore_task(php_coro_task *task);
	static inline void restore_vm_stack(php_coro_task *task);
};
}

#endif	/* COROUTINE_H */
