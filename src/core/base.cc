#include "bash.h"

wmGlobal_t WorkerG;

void workerman_base_init() {
	//初始化timer
	long now_time;
	wmGetMilliTime(&now_time);
	timerwheel_init(&WorkerG.timer, 1, now_time);
	WorkerG.is_running = false;
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
	close(WorkerG.poll->epollfd);
	free(WorkerG.poll->events);
	free(WorkerG.poll);
}

/**
 * 把数字转换成字符串
 */
int wm_itoa(char *buf, long value) {
	long i = 0, j;
	long sign_mask;
	unsigned long nn;

	sign_mask = value >> (sizeof(long) * 8 - 1);
	nn = (value + sign_mask) ^ sign_mask;
	do {
		buf[i++] = nn % 10 + '0';
	} while (nn /= 10);

	buf[i] = '-';
	i += sign_mask & 1;
	buf[i] = '\0';

	int s_len = i;
	char swap;

	for (i = 0, j = s_len - 1; i < j; ++i, --j) {
		swap = buf[i];
		buf[i] = buf[j];
		buf[j] = swap;
	}
	buf[s_len] = 0;
	return s_len;
}

/**
 * 随机
 */
int wm_rand(int min, int max) {
	static int _seed = 0;
	assert(max > min);

	if (_seed == 0) {
		_seed = time(NULL);
		srand(_seed);
	}

	int _rand = rand();
	_rand = min
			+ (int) ((double) ((double) (max) - (min) + 1.0)
					* ((_rand) / ((RAND_MAX) + 1.0)));
	return _rand;
}
