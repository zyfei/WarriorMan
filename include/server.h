#ifndef _WM_SERVER_H
#define _WM_SERVER_H

/**
 * server的头文件咯
 */
#include "bash.h"


typedef struct {
	uint32_t capacity; //容量
} wmChannel;


wmChannel* wm_channel_create(uint32_t _capacity);

//插入
bool wm_channel_push(wmChannel* channel, void *data, double timeout);

//弹出
void* wm_channel_pop(wmChannel* channel, double timeout);

//超时回调
void wm_channel_sleep_timeout(void *param);

#endif
