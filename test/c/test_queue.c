#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include "../../include/queue.h"

int main()

{
	int *p = NULL;
	wmQueue* queue = wm_queue_create();
	int i = 10, j = 11, k = 12, l = 13;
	wm_queue_push(queue, &i);
	wm_queue_push(queue, &j);
	wm_queue_push(queue, &k);
	wm_queue_push(queue, &l);
	p = (int*) wm_queue_pop(queue);
	//清空栈
	wm_queue_clear(queue);

	printf("num = %d , size = %d \n", queue->num, queue->num);
	//循环压入1000个数据
	for (i = 0; i < 13; i++) {
		int *ii = (int*) malloc(sizeof(int));
		*ii = i;
		wm_queue_push(queue, ii);
	}
	printf("num = %d , size = %d \n", queue->num, queue->num);
	for (i = 0; i < 13; i++) {
		int *p = (int*) wm_queue_pop(queue);
		if (p != NULL) {
			printf("%d \n", *p);
			free(p);
		}
	}
	printf("num = %d , size = %d \n", queue->num, queue->num);
	wm_queue_clear(queue);
	printf("======== \n");
	wm_queue_destroy(queue);

}
