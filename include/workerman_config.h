#ifndef WM_CONFIG_H_
#define WM_CONFIG_H_

#define wm_malloc              malloc
#define wm_free                free
#define wm_calloc              calloc
#define wm_realloc             realloc

enum wmEvent_type {
	WM_EVENT_NULL = 0,
	WM_EVENT_DEAULT = 1u << 8,
	WM_EVENT_READ = 1u << 9,
	WM_EVENT_WRITE  = 1u << 10,
	WM_EVENT_RDWR = WM_EVENT_READ | WM_EVENT_WRITE,
	WM_EVENT_ERROR = 1u << 11,
};

#define WM_MAXEVENTS            1024
#define WM_BUFFER_SIZE_BIG         65536

#endif /* WM_CONFIG_H_ */
