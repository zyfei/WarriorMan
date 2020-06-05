#ifndef LIST_H
#define LIST_H

/**
 * 循环双向链表结点
 */
typedef struct _wmListNode {
	struct _wmListNode *next;
	struct _wmListNode *prev;
} wmListNode;

// 初始化链表头：前后都指向自己
static inline void wmList_init(wmListNode *head) {
	head->prev = head;
	head->next = head;
}

// 插入结点到链表的前面，因为是循环链表，其实是在head的后面
static inline void wmList_add_front(wmListNode *head, wmListNode *node) {
	node->prev = head;
	node->next = head->next;
	head->next->prev = node;
	head->next = node;
}

// 插入结点到链表的后面，因为是循环链表，所以其实是在head的前面
static inline void wmList_add_back(wmListNode *head, wmListNode *node) {
	node->prev = head->prev;
	node->next = head;
	node->prev->next = node;
	head->prev = node;
}

// 判断链表是否为空：循环链表为空是头的下一个和上一个都指向自己
static inline int wmList_is_empty(wmListNode *head) {
	return head == head->next;
}

// 从链表中移除自己，同时会重设结点
static inline void wmList_remote(wmListNode *node) {
	node->next->prev = node->prev;
	node->prev->next = node->next;
	wmList_init(node);
}

// 将链表1的结点取出来，放到链表2
static inline void wmList_splice(wmListNode *head1, wmListNode *head2) {
	if (!wmList_is_empty(head1)) {
		wmListNode *first = head1->next;       // 第1个结点
		wmListNode *last = head1->prev;        // 最后1个结点
		wmListNode *at = head2->next;          // 插在第2个链表的这个结点前面
		first->prev = head2;
		head2->next = first;
		last->next = at;
		at->prev = last;
		wmList_init(head1);
	}
}

#endif
