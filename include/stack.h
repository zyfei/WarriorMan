#ifndef _STACK_H
#define _STACK_H

#include "workerman_config.h"

#define WM_STACK_INIT_LEN 10
#define WM_STACK_CRE 2

/**
 * 栈节点
 */
typedef struct _wmStack_Node {
	void* data; //数据域
} wmStack_Node;

//栈结构
typedef struct _wmStack {
	wmStack_Node* node; //栈的数据域
	int num; //栈内有几个元素
	int size; //栈的总大小
} wmStack;

//创建栈
static inline wmStack* wmStack_create() {
	long i = 0;
	wmStack* s = NULL;
	s = (wmStack*) wm_malloc(sizeof(wmStack));
	if (s == NULL) {
		return s;
	}
	//分配栈空间
	s->node = (wmStack_Node *) wm_malloc(WM_STACK_INIT_LEN * sizeof(wmStack_Node));
	//判断是否分配空间
	if (s->node == NULL) {
		wm_free(s);
		//php_error_docref(NULL, E_WARNING, "wmStack_create error");
		printf("wmStack_create error \n");
		s = NULL;
		return s;
	}
	s->num = 0;
	s->size = WM_STACK_INIT_LEN;
	//将数据域置为空
	for (i = 0; i < s->size; i++) {
		s->node[i].data = NULL;
	}
	return s;
}

//入栈
static inline int wmStack_push(wmStack *stack, void* data) {
	int i = 0;
	if (stack == NULL || stack->node == NULL) {
		return 0;
	}
	//如果溢出则增加内存分配
	if (stack->num == stack->size) {
		//重新调整大小，也就是加WM_STACK_CRE个
		stack->node = (wmStack_Node*) realloc(stack->node,
				(stack->size + WM_STACK_CRE) * sizeof(wmStack_Node));
		if (stack->node == NULL) {
			printf("wmStack_push error \n");
			return 0;
		}
		int old_size = stack->size;
		stack->size += WM_STACK_CRE;
		//将重新分配的数据域置为NULL
		for (i = old_size; i < stack->size; i++) {
			stack->node[i].data = NULL;
		}
	}
	stack->node[stack->num].data = data;
	stack->num++;
	return 1;
}

/**
 * //出栈
 */
static inline void* wmStack_pop(wmStack *stack) {
	void* data = NULL;
	if (stack == NULL || stack->node == NULL) {
		return NULL;
	}
	if (stack->num == 0) {
		return NULL;
	}
	data = stack->node[stack->num - 1].data; //弹出栈顶内容
	stack->node[stack->num - 1].data = NULL; //栈顶数据域置空
	stack->num--; //栈顶减1
	return data;
}

//获取栈长度
static inline int wmStack_len(wmStack *stack) {
	return stack->num;
}

//清空栈
static inline void wmStack_clear(wmStack *stack) {
	long i = 0;
	if (stack == NULL || stack->node == NULL) {
		return;
	}
	for (i = stack->num - 1; i >= 0; i--) {
		stack->node[i].data = NULL;
	}
	stack->num = 0;
}

//销毁栈
static inline void wmStack_destroy(wmStack *stack) {
	if (stack == NULL)
		return;
	if (stack->node != NULL) {
		wm_free(stack->node);
	}
	wm_free(stack);
}

#endif
