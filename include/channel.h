#ifndef _WM_CHANNEL_H
#define _WM_CHANNEL_H
/**
 * 协程化channel
 */
#include "base.h"

typedef struct {
	uint32_t capacity; //容量
	//生产者协程等待队列
	wmQueue *producer_queue;
	//消费者协程等待队列
	wmQueue *consumer_queue;
	//这个里面存着，存入channle中的数据
	wmQueue *data_queue;
} wmChannel;

extern swHashMap *wm_channels;
wmChannel* wmChannel_create(uint32_t _capacity);
bool wmChannel_push(wmChannel* channel, void *data, double timeout);//插入
void* wmChannel_pop(wmChannel* channel, double timeout);//弹出
int wmChannel_num(wmChannel* channel);//协程内多少元素
void wmChannel_clear(wmChannel* channel);//清空这个协程的所有元素
void wmChannel_free(wmChannel* channel);//销毁

#endif
