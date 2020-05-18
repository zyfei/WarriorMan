#ifndef _WM_CHANNEL_H
#define _WM_CHANNEL_H
/**
 * 协程化channel
 */
#include "bash.h"

enum channel_opcode {
	CHANNEL_PUSH = 1, CHANNEL_POP = 2,
};

typedef struct {
	uint32_t capacity; //容量
} wmChannel;


extern swHashMap *wm_channels;

wmChannel* wm_channel_create(uint32_t _capacity);

//插入
bool wm_channel_push(wmChannel* channel, void *data, double timeout);

//弹出
void* wm_channel_pop(wmChannel* channel, double timeout);

//插入
int wm_channel_num(wmChannel* channel);

//情况这个协程的所有元素
void wm_channel_clear(wmChannel* channel);

//超时回调
void wm_channel_sleep_timeout(void *param);

#endif
