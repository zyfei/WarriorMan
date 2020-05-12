#ifndef ERROR_H
#define ERROR_H

#include "header.h"

enum wmErrorCode {
	/**
	 * connection error
	 */
	WM_ERROR_SESSION_CLOSED_BY_SERVER = 1001, //代表连接是被服务器关闭的，
	WM_ERROR_SESSION_CLOSED_BY_CLIENT = 1002, //代表连接是被客户端关闭的。
};

const char* wm_strerror(int code);

#endif    /* ERROR_H */
