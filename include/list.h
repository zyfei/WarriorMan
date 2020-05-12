#ifndef LIST_H
#define LIST_H

/**
 * 循环双向链表结点
 */
typedef struct clinknode {
	struct clinknode *next;
	struct clinknode *prev;
} clinknode_t;

// 初始化链表头：前后都指向自己
static inline void clinklist_init(clinknode_t *head) {
	head->prev = head;
	head->next = head;
}

// 插入结点到链表的前面，因为是循环链表，其实是在head的后面
static inline void clinklist_add_front(clinknode_t *head, clinknode_t *node) {
	node->prev = head;
	node->next = head->next;
	head->next->prev = node;
	head->next = node;
}

// 插入结点到链表的后面，因为是循环链表，所以其实是在head的前面
static inline void clinklist_add_back(clinknode_t *head, clinknode_t *node) {
	node->prev = head->prev;
	node->next = head;
	node->prev->next = node;
	head->prev = node;
}

// 判断链表是否为空：循环链表为空是头的下一个和上一个都指向自己
static inline int clinklist_is_empty(clinknode_t *head) {
	return head == head->next;
}

// 从链表中移除自己，同时会重设结点
static inline void clinklist_remote(clinknode_t *node) {
	node->next->prev = node->prev;
	node->prev->next = node->next;
	clinklist_init(node);
}

// 将链表1的结点取出来，放到链表2
static inline void clinklist_splice(clinknode_t *head1, clinknode_t *head2) {
	if (!clinklist_is_empty(head1)) {
		clinknode_t *first = head1->next;       // 第1个结点
		clinknode_t *last = head1->prev;        // 最后1个结点
		clinknode_t *at = head2->next;          // 插在第2个链表的这个结点前面
		first->prev = head2;
		head2->next = first;
		last->next = at;
		at->prev = last;
		clinklist_init(head1);
	}
}

#endif
