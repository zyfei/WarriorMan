#include "worker_coroutine.h"

void sleep_callback(void* co);

void wmCoroutine_sleep(double seconds) {
	if (seconds < 0.001) {
		seconds = 0.001;
	}
	wmCoroutine* co = wmCoroutine_get_current();
	wmTimerWheel_add_quick(&WorkerG.timer, sleep_callback, (void*) co,
			seconds * 1000);
	wmCoroutine_yield(co);
}

//sleep回调
void sleep_callback(void* co) {
	wmCoroutine_resume((wmCoroutine*) co);
}
