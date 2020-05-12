#include "header.h"

wmGlobal_t WorkerG;

void workerman_base_init() {
	//初始化timer
	long now_time;
	wmGetMilliTime(&now_time);
	timerwheel_init(&WorkerG.timer, 1, now_time);
}

//初始化epoll
void init_wmPoll() {
	WorkerG.poll = (wmPoll_t *) malloc(sizeof(wmPoll_t));
	size_t size;
	WorkerG.poll->epollfd = epoll_create(512); //创建一个epollfd，然后保存在全局变量
	WorkerG.poll->ncap = WM_MAXEVENTS; //有16个event
	size = sizeof(struct epoll_event) * WorkerG.poll->ncap;
	WorkerG.poll->events = (struct epoll_event *) malloc(size);
	memset(WorkerG.poll->events, 0, size);
}

void free_wmPoll() {
	free(WorkerG.poll->events);
	free(WorkerG.poll);
}
