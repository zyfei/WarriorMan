#include "coroutine.h"

using namespace std;
using namespace workerman;

Context::Context(size_t stack_size, coroutine_func_t fn, void* private_data) :
		fn_(fn), stack_size_(stack_size), private_data_(private_data) {
	swap_ctx_ = nullptr;

	//是创建一个C栈（实际上是从堆中分配的内存）。
	stack_ = (char*) wm_malloc(stack_size_);

	//代码是把堆模拟成栈的行为。与之前PHP栈的操作类似。
	void* sp = (void*) ((char*) stack_ + stack_size_);

	//这行代码是设置这个最底层的协程的上下文ctx_，比如栈地址，栈大小，协程的入口函数context_func。
	//而make_fcontext这个设置上下文的函数式用的boost.asm里面的库。
	ctx_ = make_fcontext(sp, stack_size_, (void (*)(intptr_t))&context_func); //
};

//每次删除所创建的对象时执行
Context::~Context() {
	if (stack_) {
		//施放内存
		wm_free(stack_);
		stack_ = NULL;
	}
}

/**
 * 执行创建php协程，并且切换上下文
 */
void Context::context_func(void *arg) {
	Context *_this = (Context *) arg;
	//调用了fn，也就是WorkerCoroutine::create_func
	_this->fn_(_this->private_data_);

	//swap_out的作用是让出当前协程的上下文，去加载其他协程的上下文。
	//就是当我们跑完了这个协程，需要恢复其他的协程的上下文，让其他的协程继续运行。我们要应该把当前执行的php方法栈弹出,出栈操作.我们在run里面操作
	_this->end_ = true;
	_this->swap_out();
}

/**
 * 加载上下文
 */
bool Context::swap_in() {
	jump_fcontext(&swap_ctx_, ctx_, (intptr_t) this, true);
	return true;
}

/**
 * jump_fcontext这个函数也是boost.asm库提供的
 */
bool Context::swap_out() {
	jump_fcontext(&ctx_, swap_ctx_, (intptr_t) this, true);
	return true;
}

