#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../core/timer.cc"

void timer_call(void *arg) {
	uint64_t t = *(uint64_t *) arg;
	//uint64_t t = (uint64_t) arg;
	printf("==> %ld \n", t);
}

int main() {
	uint64_t time = 0;
	timerwheel_t timer;

	timerwheel_init(&timer, 1, time);

	//第一个轮范围 2   第二个轮 4 8 16 32

	timernode_t node1,node2,node3;
	timerwheel_node_init(&node1, timer_call, (void*) &time);
	timerwheel_node_init(&node2, timer_call, (void*) &time);
	timerwheel_node_init(&node3, timer_call, (void*) &time);

//	timerwheel_add(&timer,&node1,0);
//	timerwheel_add(&timer,&node2,12);
//	timerwheel_add(&timer,&node3,35);
//	timerwheel_add(&timer,&node,3);
//	timerwheel_add(&timer,&node,9);
//	timerwheel_add(&timer,&node,31);
	//timerwheel_add(&timer,&node,33);

	for (int i = 0; i < 35; i++) {
		timerwheel_update(&timer,++time);
	}

	return 0;
}
