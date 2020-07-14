#include "log.h"

char wm_debug[WM_DEBUG_MSG_SIZE];
char wm_trace[WM_TRACE_MSG_SIZE];
char wm_warn[WM_WARN_MSG_SIZE];
char wm_error[WM_ERROR_MSG_SIZE];

void wmLog_put(int level, char *cnt) {
	const char *level_wmr;
	char date_wmr[WM_LOG_DATE_WMRLEN];
	char log_wmr[WM_LOG_BUFFER_SIZE];
	int n;
	time_t t;
	struct tm *p;

	switch (level) {
	case WM_LOG_DEBUG:
		level_wmr = "DEBUG";
		break;
	case WM_LOG_NOTICE:
		level_wmr = "NOTICE";
		break;
	case WM_LOG_ERROR:
		level_wmr = "ERROR";
		break;
	case WM_LOG_WARNING:
		level_wmr = "WARNING";
		break;
	case WM_LOG_TRACE:
		level_wmr = "TRACE";
		break;
	default:
		level_wmr = "INFO";
		break;
	}

	t = time(NULL);
	p = localtime(&t);
	snprintf(date_wmr, WM_LOG_DATE_WMRLEN, "%d-%02d-%02d %02d:%02d:%02d", p->tm_year + 1900, p->tm_mon + 1, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
	n = snprintf(log_wmr, WM_LOG_BUFFER_SIZE, "[%s]\t%s\t%s\n", date_wmr, level_wmr, cnt);
	if (write(STDOUT_FILENO, log_wmr, n) < 0) {
		php_printf("write(log_fd, size=%d) failed. Error: %s[%d].\n", n, strerror(errno), errno);
	}
}

const char* wmCode_str(int code) {
	if (code < 1000) {
		return strerror(code);
	}
	switch (code) {
	case WM_ERROR_SESSION_CLOSED_BY_SERVER:
		return "Session closed by worker";
		break;
	case WM_ERROR_SESSION_CLOSED_BY_CLIENT:
		return "Session closed by client";
		break;
	case WM_ERROR_SESSION_CLOSED:
		return "Session has closed";
		break;
	case WM_ERROR_SEND_FAIL:
		return "send buffer full and drop package";
		break;
	case WM_ERROR_READ_FAIL:
		return "read buffer error";
		break;
	case WM_ERROR_LOOP_FAIL:
		return "loop error";
		break;
	case WM_ERROR_SEND_BUFFER_FULL:
		return "send buffer full";
		break;
	case WM_ERROR_PROTOCOL_FAIL:
		return "protocol fail";
		break;
	default:
		snprintf(wm_error, sizeof(wm_error), "Unknown error: %d", code);
		return wm_error;
		break;
	}
}

