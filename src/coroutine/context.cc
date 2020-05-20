#include "context.h"

void wmContext_func(void *arg);

/**
 * 初始化Context
 */
void wmContext_init(wmContext *ctx, size_t stack_size, coroutine_func_t fn,
		void* private_data) {
	ctx->end_ = false;
	ctx->stack_size_ = stack_size;
	ctx->private_data_ = private_data;
	ctx->fn_ = fn;
	ctx->swap_ctx_ = NULL;
	//是创建一个C栈（实际上是从堆中分配的内存）。
	ctx->stack_ = (char*) wm_malloc(ctx->stack_size_);

	//传入模拟的stack的结束指针位置
	//代码是把堆模拟成栈的行为。与之前PHP栈的操作类似。
	void* sp = (void*) ((char*) ctx->stack_ + ctx->stack_size_);

	//这行代码是设置这个最底层的协程的上下文ctx_，比如栈地址，栈大小，协程的入口函数context_func。
	//而make_fcontext这个设置上下文的函数式用的boost.asm里面的库。
	ctx->ctx_ = make_fcontext(sp, ctx->stack_size_,
			(void (*)(intptr_t))&wmContext_func); //
};

/**
 * 执行php协程，并且切换上下文
 */
void wmContext_func(void *arg) {

	wmContext *_this = (wmContext *) arg;
	//调用了fn，也就是WorkerCoroutine::create_func
	_this->fn_(_this->private_data_);

	//swap_out的作用是让出当前协程的上下文，去加载其他协程的上下文。
	//就是当我们跑完了这个协程，需要恢复其他的协程的上下文，让其他的协程继续运行。我们要应该把当前执行的php方法栈弹出,出栈操作.我们在run里面操作
	_this->end_ = true;

	//执行完在这里直接让出控制权
	wmContext_swap_out(_this);
}

/**
 * 加载上下文,也就是执行这个context对于的php代码段
 */
bool wmContext_swap_in(wmContext *ctx) {
	jump_fcontext(&ctx->swap_ctx_, ctx->ctx_, (intptr_t) ctx, true);
	return true;
}

/**
 * jump_fcontext这个函数也是boost.asm库提供的
 */
bool wmContext_swap_out(wmContext *ctx) {
	jump_fcontext(&ctx->ctx_, ctx->swap_ctx_, (intptr_t) ctx, true);
	return true;
}

//每次删除所创建的对象时执行
void wmContext_destroy(wmContext *ctx) {
	if (ctx->stack_) {
		//施放内存
		wm_free(ctx->stack_);
		ctx->stack_ = NULL;
	}

}
