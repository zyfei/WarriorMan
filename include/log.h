#ifndef LOG_H_
#define LOG_H_

#include "header.h"

#define WM_LOG_BUFFER_SIZE 1024
#define WM_LOG_DATE_WMRLEN  64

#define WM_DEBUG_MSG_SIZE 512
#define WM_TRACE_MSG_SIZE 512
#define WM_WARN_MSG_SIZE 512
#define WM_ERROR_MSG_SIZE 512

extern char wm_debug[WM_DEBUG_MSG_SIZE];
extern char wm_trace[WM_TRACE_MSG_SIZE];
extern char wm_warn[WM_WARN_MSG_SIZE];
extern char wm_error[WM_ERROR_MSG_SIZE];

enum wmCode {
	/**
	 * connection error
	 */
	WM_ERROR_SESSION_CLOSED_BY_SERVER = 1001, //代表连接是被服务器关闭的，
	WM_ERROR_SESSION_CLOSED_BY_CLIENT = 1002, //代表连接是被客户端关闭的。
	WM_ERROR_SESSION_CLOSED = 1003, //代表连接是被客户端关闭的。
	WM_ERROR_SEND_FAIL = 1004, //发送失败
	WM_ERROR_READ_FAIL = 1005, //接收失败
	WM_ERROR_LOOP_FAIL = 1006, //LOOP回调错误
};

#define wmDebug(wmr, ...)                                                         \
    snprintf(wm_debug, WM_DEBUG_MSG_SIZE, "%s: " wmr " in %s on line %d.", __func__, ##__VA_ARGS__, __FILE__, __LINE__); \
    wmLog_put(WM_LOG_DEBUG, wm_debug);

#define wmTrace(wmr, ...)                                                         \
    snprintf(wm_trace, WM_TRACE_MSG_SIZE, "%s: " wmr " in %s on line %d.", __func__, ##__VA_ARGS__, __FILE__, __LINE__); \
    wmLog_put(WM_LOG_TRACE, wm_trace);

#define wmWarn(wmr, ...)                                                         \
    snprintf(wm_error, WM_ERROR_MSG_SIZE, "%s: " wmr " in %s on line %d.", __func__, ##__VA_ARGS__, __FILE__, __LINE__); \
    wmLog_put(WM_LOG_WARNING, wm_error);

#define wmError(wmr, ...)                                                         \
    snprintf(wm_error, WM_ERROR_MSG_SIZE, "%s: " wmr " in %s on line %d.", __func__, ##__VA_ARGS__, __FILE__, __LINE__); \
    wmLog_put(WM_LOG_ERROR, wm_error); \
    exit(-1);

enum wmLog_level {
	WM_LOG_DEBUG = 0, WM_LOG_TRACE, WM_LOG_INFO, WM_LOG_NOTICE, WM_LOG_WARNING, WM_LOG_ERROR,
};

void wmLog_put(int level, char *cnt);

const char* wmCode_str(int code);

#endif /* LOG_H_ */
