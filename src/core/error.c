#include "error.h"
#include "log.h"

const char* wm_strerror(int code) {
	switch (code) {
	case WM_ERROR_SESSION_CLOSED_BY_SERVER:
		return "Session closed by server";
		break;
	case WM_ERROR_SESSION_CLOSED_BY_CLIENT:
		return "Session closed by client";
		break;
	default:
		snprintf(wm_error, sizeof(wm_error), "Unknown error: %d", code);
		return wm_error;
		break;
	}
}
