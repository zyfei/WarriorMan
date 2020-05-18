#ifndef _WM_QUEUE_H
#define _WM_QUEUE_H

#include "workerman_config.h"
#include "list.h"

// 定时器结点
typedef struct {
	clinknode_t *next; // 下一个结点
	clinknode_t *prev; // 上一个结点
	void *data; // 用户数据
} wmQueueNode;

// 队列结构体
typedef struct {
	uint32_t num; //队列元素个数
	wmQueueNode head; //队列头,头是不包含元素的
} wmQueue;

// 初始化队列
static inline wmQueue* wm_queue_create() {
	wmQueue* queue = (wmQueue *) wm_malloc(sizeof(wmQueue));
	bzero(queue, sizeof(wmQueue));

	queue->num = 0;
	clinklist_init((clinknode_t *) &queue->head);
	return queue;
}

// push
static inline void wm_queue_push(wmQueue* queue, void *data) {
	wmQueueNode* node = (wmQueueNode *) wm_malloc(sizeof(wmQueueNode));
	bzero(node, sizeof(wmQueueNode));
	clinklist_init((clinknode_t *) node);
	//保存用户数据
	node->data = data;
	//添加到整个双向列表的最后
	clinklist_add_back((clinknode_t *) (&queue->head), (clinknode_t *) node);
	queue->num++;
}

// pop
static inline void * wm_queue_pop(wmQueue* queue) {
	//如果没有元素了
	if (clinklist_is_empty((clinknode_t *) (&queue->head)) || queue->num == 0) {
		return NULL;
	}
	clinknode_t* next = queue->head.next;
	clinklist_remote(next);
	//释放
	wm_free(((wmQueueNode*) next));
	//减少记数
	queue->num--;
	return ((wmQueueNode*) next)->data;
}

//获取长度
static inline int wm_queue_len(wmQueue* queue) {
	return queue->num;
}

//清空队列
static inline void wm_queue_clear(wmQueue* queue) {
	if (queue == NULL || queue->num == 0) {
		return;
	}
	clinknode_t* head = (clinknode_t *) &queue->head;
	while (!clinklist_is_empty(head)) {
		wmQueueNode* _wqn = (wmQueueNode*) head->next;
		clinklist_remote(head->next);
		wm_free(_wqn);
	}
	queue->num = 0;
}

//销毁
static inline void wm_queue_destroy(wmQueue* queue) {
	wm_queue_clear(queue);
	wm_free(queue);
	queue = NULL;
}

#endif
