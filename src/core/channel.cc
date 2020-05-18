#include "channel.h"

using workerman::Coroutine;

//协程map表
swHashMap *wm_channels = NULL;

//生产者协程等待队列
static wmQueue *producer_queue = NULL;
//消费者协程等待队列
static wmQueue *consumer_queue = NULL;

//这个里面存着，存入channle中的数据
static wmQueue *data_queue = NULL;

//这个结构体，是用来控制协程，不在错误的时间co->resume的
typedef struct {
	Coroutine* co;
	bool type; //是否需要被唤醒
} WmChannelCoroutineType;

wmChannel* wm_channel_create(uint32_t _capacity) {
	if (!wm_channels) {
		//协程map表
		wm_channels = swHashMap_new(NULL);
	}
	if (!producer_queue) {
		//生产者协程队列
		producer_queue = wm_queue_create();
	}
	if (!consumer_queue) {
		//消费者协程队列
		consumer_queue = wm_queue_create();
	}
	if (!data_queue) {
		//存储Channel使用到的数据。
		data_queue = wm_queue_create();
	}
	wmChannel* channel = (wmChannel *) malloc(sizeof(wmChannel));
	bzero(channel, sizeof(wmChannel));
	channel->capacity = _capacity;
	return channel;
}

//插入
bool wm_channel_push(wmChannel* channel, void *data, double timeout) {
	//获取当前协程
	Coroutine *co = Coroutine::get_current();
	//如果当前channel内容，已到channel上限
	if (data_queue->num == channel->capacity) {
		WmChannelCoroutineType* wct = NULL;
		//如果设置了超时时间
		if (timeout > 0) {
			wct = (WmChannelCoroutineType *) malloc(
					sizeof(WmChannelCoroutineType));
			wct->co = co;
			wct->type = true;
			//就添加到定时器中,定时器到时间，会把当前这个协程再唤醒
			timerwheel_add_quick(&WorkerG.timer, wm_channel_sleep_timeout,
					(void*) wct, timeout * 1000);
		}
		//把当前协程，加入生产者协程等待队列中
		wm_queue_push(producer_queue, co);
		//协程暂时yield，等待定时器超时时间结束，或者消费者通知
		co->yield();
		//协程已经醒了，就不需要被唤醒了
		if (wct) {
			wct->type = false;
		}
	}

	// 定时器结束，或者未设置timeout

	/**
	 * 这个时候如果channel还是满的，那直接返回false，我存不进去
	 */
	if (data_queue->num == channel->capacity) {
		return false;
	}

	//如果没满的话，就把数据加入channel队列中，这个时候channel中已经有数据了
	wm_queue_push(data_queue, data);

	/**
	 * 那就通知消费者，来消费
	 */

	while (consumer_queue->num > 0) {
		//唤醒一个消费者
		co = (Coroutine*) wm_queue_pop(consumer_queue);
		if (!co) {
			continue;
		}
		co->resume();
	}
	//消费者协程退出控制权的话，那么这边也返回
	return true;
}

//弹出
void* wm_channel_pop(wmChannel* channel, double timeout) {
	//获取当前协程
	Coroutine *co = Coroutine::get_current();
	//准备接受pop的数据
	void *data;

	//如果当前channel已经空了,也就是弹不出来了
	if (data_queue->num == 0) {
		WmChannelCoroutineType* wct = NULL;
		//如果设置了超时时间
		if (timeout > 0) {
			wct = (WmChannelCoroutineType *) malloc(
					sizeof(WmChannelCoroutineType));
			wct->co = co;
			wct->type = true;
			//就添加到定时器中,定时器到时间，会把这个协程再唤醒
			timerwheel_add_quick(&WorkerG.timer, wm_channel_sleep_timeout,
					(void*) co, timeout * 1000);
		}
		//加入消费者等待队列中
		wm_queue_push(consumer_queue, co);
		//协程暂时yield，等待定时器超时时间结束
		co->yield();
		//协程已经醒了，就不需要被定时器唤醒了
		if (wct) {
			wct->type = false;
		}
	}

	//协程timeout恢复运行的时候.如果还是没有等到channel数据，那么返回空
	if (data_queue->num == 0) {
		return nullptr;
	}

	//取一个数据
	data = wm_queue_pop(data_queue);

	/**
	 * 通知生产者
	 */
	while (producer_queue->num > 0) {
		//然后如果有生产者协程在等待，那么就resume那个生产者协程。
		//最后，等生产者协程执行完毕，或者生产者协程主动yield，才会回到消费者协程，最后返回data。
		co = (Coroutine*) wm_queue_pop(producer_queue);
		//如果这个生产者，等不及已经退出了
		if (!co) {
			continue;
		}
		co->resume();
	}
	return data;
}

/**
 * 协程内多少元素
 */
int wm_channel_num(wmChannel* channel) {
	return data_queue->num;
}

void wm_channel_clear(wmChannel* channel) {
	zval *data;
	while (wm_channel_num(channel) > 0) {
		data = (zval *) wm_queue_pop(data_queue);
		zval_ptr_dtor(data);
		efree(data);
	}
	data = NULL;
}

/**
 * 超时
 */
void wm_channel_sleep_timeout(void *param) {
	WmChannelCoroutineType * wct = ((WmChannelCoroutineType *) param);
	Coroutine * co = wct->co;
	bool type = wct->type;
	free(wct);
	wct = NULL;
	//是否需要被唤醒
	if (type) {
		//让协程恢复原来的执行状态
		co->resume();
	}
}

