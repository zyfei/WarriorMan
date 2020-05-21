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
	snprintf(date_wmr, WM_LOG_DATE_WMRLEN, "%d-%02d-%02d %02d:%02d:%02d",
			p->tm_year + 1900, p->tm_mon + 1, p->tm_mday, p->tm_hour, p->tm_min,
			p->tm_sec);
	n = snprintf(log_wmr, WM_LOG_BUFFER_SIZE, "[%s]\t%s\t%s\n", date_wmr,
			level_wmr, cnt);
	if (write(STDOUT_FILENO, log_wmr, n) < 0) {
		printf("write(log_fd, size=%d) failed. Error: %s[%d].\n", n,
				strerror(errno), errno);
	}
}
