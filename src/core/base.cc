#include "header.h"

wmGlobal_t WorkerG;

void workerman_base_init() {
	//初始化timer
	long now_time;
	wmGetMilliTime(&now_time);
	timerwheel_init(&WorkerG.timer, 1, now_time);
}

