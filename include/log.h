#ifndef LOG_H_
#define LOG_H_

#include "header.h"

#define WM_OK 0
#define WM_ERR -1

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

enum wmLog_level
{
    WM_LOG_DEBUG = 0,
    WM_LOG_TRACE,
    WM_LOG_INFO,
    WM_LOG_NOTICE,
    WM_LOG_WARNING,
    WM_LOG_ERROR,
};

void wmLog_put(int level, char *cnt);

#endif /* LOG_H_ */
