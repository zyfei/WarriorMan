#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../src/core/timer.c"

void timer_call(void *arg) {
	uint64_t t = *(uint64_t *) arg;
	//uint64_t t = (uint64_t) arg;
	printf("==> %ld \n", t);
}

int main() {
	uint64_t time = 0;
	wmTimerWheel timer;

	wmTimerWheel_init(&timer, 1, time);

	//第一个轮范围 2   第二个轮 4 8 16 32

	wmTimerWheel_Node node1,node2,node3;
	wmTimerWheel_node_init(&node1, timer_call, (void*) &time);
	wmTimerWheel_node_init(&node2, timer_call, (void*) &time);
	wmTimerWheel_node_init(&node3, timer_call, (void*) &time);

//	wmTimerWheel_add(&timer,&node1,0);
//	wmTimerWheel_add(&timer,&node2,12);
//	wmTimerWheel_add(&timer,&node3,35);
//	wmTimerWheel_add(&timer,&node,3);
//	wmTimerWheel_add(&timer,&node,9);
//	wmTimerWheel_add(&timer,&node,31);
	//wmTimerWheel_add(&timer,&node,33);

	for (int i = 0; i < 35; i++) {
		wmTimerWheel_update(&timer,++time);
	}

	return 0;
}
