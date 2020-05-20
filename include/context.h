#ifndef WM_CONTEXT_H
#define WM_CONTEXT_H

#include "bash.h"
#include "asm_context.h"

typedef fcontext_t coroutine_context_t;
typedef void (*coroutine_func_t)(void*);

/**
 * 协程context结构体
 */
typedef struct {
	coroutine_func_t fn_;
	uint32_t stack_size_;
	void *private_data_;
	char* stack_;
	//指向汇编
	coroutine_context_t ctx_;
	coroutine_context_t swap_ctx_;
	bool end_;
} wmContext;

void wmContext_init(wmContext *ctx, size_t stack_size, coroutine_func_t fn,
		void* private_data);
bool wmContext_swap_out(wmContext *ctx);
bool wmContext_swap_in(wmContext *ctx);
void wmContext_destroy(wmContext *ctx);

#endif	/* WM_CONTEXT_H */
