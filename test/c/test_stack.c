#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include<malloc.h>

#include "../include/stack.h"

int main()

{
	int *p = NULL;
	wmStack* stack = wmStack_create();
	int i = 10, j = 11, k = 12, l = 13;
	wmStack_push(stack, &i);
	wmStack_push(stack, &j);
	wmStack_push(stack, &k);
	wmStack_push(stack, &l);
	p = (int*) wmStack_pop(stack);
	//清空栈
	wmStack_clear(stack);

	printf("num = %d , size = %d \n", stack->num, stack->size);
	//循环压入1000个数据
	for (i = 0; i < 13; i++) {
		int *ii = (int*) malloc(sizeof(int));
		*ii = i;
		wmStack_push(stack, ii);
	}
	printf("num = %d , size = %d \n", stack->num, stack->size);
	for (i = 0; i < 13; i++) {
		int *p = (int*) wmStack_pop(stack);
		if (p != NULL) {
			printf("%d \n", *p);
		}
	}
	printf("num = %d , size = %d \n", stack->num, stack->size);
	wmStack_clear(stack);
	printf("======== \n");
	//释放数据内存
	for (i = 0; i < 13; i++) {
		free((int*) wmStack_pop(stack));
	}
	wmStack_destroy(stack);

}
