#ifndef _WM_QUEUE_H
#define _WM_QUEUE_H

#include "workerman_config.h"
#include "list.h"

// 定时器结点
typedef struct {
	wmListNode *next; // 下一个结点
	wmListNode *prev; // 上一个结点
	void *data; // 用户数据
} wmQueueNode;

// 队列结构体
typedef struct {
	uint32_t num; //队列元素个数
	wmQueueNode head; //队列头,头是不包含元素的
} wmQueue;

// 初始化队列
static inline wmQueue* wmQueue_create() {
	wmQueue* queue = (wmQueue *) wm_malloc(sizeof(wmQueue));
	bzero(queue, sizeof(wmQueue));

	queue->num = 0;
	wmList_init((wmListNode *) &queue->head);
	return queue;
}

// push
static inline void wmQueue_push(wmQueue* queue, void *data) {
	wmQueueNode* node = (wmQueueNode *) wm_malloc(sizeof(wmQueueNode));
	bzero(node, sizeof(wmQueueNode));
	wmList_init((wmListNode *) node);
	//保存用户数据
	node->data = data;
	//添加到整个双向列表的最后
	wmList_add_back((wmListNode *) (&queue->head), (wmListNode *) node);
	queue->num++;
}

// pop
static inline void * wmQueue_pop(wmQueue* queue) {
	//如果没有元素了
	if (wmList_is_empty((wmListNode *) (&queue->head)) || queue->num == 0) {
		return NULL;
	}
	wmListNode* next = queue->head.next;
	wmList_remote(next);
	//释放
	wm_free(((wmQueueNode*) next));
	//减少记数
	queue->num--;
	return ((wmQueueNode*) next)->data;
}

//获取长度
static inline int wmQueue_len(wmQueue* queue) {
	return queue->num;
}

//清空队列
static inline void wmQueue_clear(wmQueue* queue) {
	if (queue == NULL || queue->num == 0) {
		return;
	}
	wmListNode* head = (wmListNode *) &queue->head;
	while (!wmList_is_empty(head)) {
		wmQueueNode* _wqn = (wmQueueNode*) head->next;
		wmList_remote(head->next);
		wm_free(_wqn);
	}
	queue->num = 0;
}

//销毁
static inline void wmQueue_destroy(wmQueue* queue) {
	wmQueue_clear(queue);
	wm_free(queue);
	queue = NULL;
}

#endif
