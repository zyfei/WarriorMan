#include "coroutine.h"

using namespace std;
using workerman::Coroutine;

Coroutine* Coroutine::current = nullptr;
long Coroutine::last_cid = 0;

swHashMap *Coroutine::coroutines = swHashMap_new(NULL);

size_t Coroutine::stack_size = DEFAULT_C_STACK_SIZE;

st_coro_on_swap_t Coroutine::on_yield = nullptr;
st_coro_on_swap_t Coroutine::on_resume = nullptr;
st_coro_on_swap_t Coroutine::on_close = nullptr;

/**
 * 创建一个协程，然后让它运行。
 */
long Coroutine::create(coroutine_func_t fn, void* args) {
	return (new Coroutine(fn, args))->run();
}

/**
 * 获取当前协程
 */
void* Coroutine::get_current_task() {
	return current ? current->get_task() : nullptr;
}

/**
 * 获取自身协程任务
 */
void* Coroutine::get_task() {
	return task;
}

void Coroutine::set_task(void *_task) {
	task = _task;
}

/**
 * 返回当前协程类
 */
Coroutine* Coroutine::get_current() {
	return current;
}

/**
 * 通过cid获取协程
 */
Coroutine* Coroutine::get_by_cid(long _cid) {
	Coroutine* _co = (Coroutine*)swHashMap_find_int(coroutines,_cid);
	return _co;
}

void Coroutine::set_on_close(st_coro_on_swap_t func) {
	on_close = func;
}

void Coroutine::set_on_yield(st_coro_on_swap_t func) {
	on_yield = func;
}

void Coroutine::set_on_resume(st_coro_on_swap_t func) {
	on_resume = func;
}

void Coroutine::yield() {
	assert(current == this);
	on_yield(task);

	current = origin;
	ctx.swap_out();
}

void Coroutine::resume() {
	assert(current != this);
	on_resume(task);
	origin = current;
	current = this;
	ctx.swap_in();
	if (ctx.is_end()) {
		assert(current == this);
		on_close(task);
		current = origin;
		swHashMap_del_int(coroutines, cid);
		delete this;
	}
}
