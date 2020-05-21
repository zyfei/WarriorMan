#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include "../../include/queue.h"

int main()

{
	int *p = NULL;
	wmQueue* queue = wmQueue_create();
	int i = 10, j = 11, k = 12, l = 13;
	wmQueue_push(queue, &i);
	wmQueue_push(queue, &j);
	wmQueue_push(queue, &k);
	wmQueue_push(queue, &l);
	p = (int*) wmQueue_pop(queue);
	//清空栈
	wmQueue_clear(queue);

	printf("num = %d , size = %d \n", queue->num, queue->num);
	//循环压入1000个数据
	for (i = 0; i < 13; i++) {
		int *ii = (int*) malloc(sizeof(int));
		*ii = i;
		wmQueue_push(queue, ii);
	}
	printf("num = %d , size = %d \n", queue->num, queue->num);
	for (i = 0; i < 13; i++) {
		int *p = (int*) wmQueue_pop(queue);
		if (p != NULL) {
			printf("%d \n", *p);
			free(p);
		}
	}
	printf("num = %d , size = %d \n", queue->num, queue->num);
	wmQueue_clear(queue);
	printf("======== \n");
	wmQueue_destroy(queue);

}
