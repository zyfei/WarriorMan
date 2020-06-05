#include "base.h"
#include "coroutine.h"

wmGlobal_t WorkerG;

void workerman_base_init() {
	//初始化timer
	long now_time;
	wmGetMilliTime(&now_time);
	wmTimerWheel_init(&WorkerG.timer, 1, now_time);
	WorkerG.is_running = false;
	WorkerG.poll = NULL;
	WorkerG.buffer_stack = wmString_new(512);
}

//初始化epoll
int init_wmPoll() {
	if (!WorkerG.poll) {
		size_t size;
		WorkerG.poll = (wmPoll_t *) wm_malloc(sizeof(wmPoll_t));
		if (WorkerG.poll == NULL) {
			wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
			return -1;
		}
		WorkerG.poll->epollfd = epoll_create(512); //创建一个epollfd，然后保存在全局变量
		if (WorkerG.poll->epollfd < 0) {
			wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
			wm_free(WorkerG.poll);
			WorkerG.poll = NULL;
			return -1;
		}
		WorkerG.poll->ncap = WM_MAXEVENTS;
		size = sizeof(struct epoll_event) * WorkerG.poll->ncap;
		WorkerG.poll->events = (struct epoll_event *) wm_malloc(size);
		memset(WorkerG.poll->events, 0, size);
		WorkerG.poll->event_num = 0; // 事件的数量
	}
	return 0;
}

//释放epoll
int free_wmPoll() {
	if (close(WorkerG.poll->epollfd) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
	}
	wm_free(WorkerG.poll->events);
	WorkerG.poll->events = NULL;
	wm_free(WorkerG.poll);
	WorkerG.poll = NULL;
	WorkerG.is_running = false;
	return 0;
}

//普通调度器，server的使用不用
int wm_event_wait() {
	init_wmPoll();
	if (!WorkerG.poll) {
		wmError("Need to call init_wmPoll() first.");
	}
	WorkerG.is_running = true;

	long mic_time;
	//这里应该改成死循环了
	while (WorkerG.is_running) {
		int n;
		//毫秒级定时器，必须是1
		int timeout = 1;
		struct epoll_event *events;
		events = WorkerG.poll->events;
		n = epoll_wait(WorkerG.poll->epollfd, events, WorkerG.poll->ncap, timeout);
		//循环处理epoll请求
		for (int i = 0; i < n; i++) {
			int fd;
			int id;
			struct epoll_event *p = &events[i];
			//if(p->events & EPOLLIN)

			uint64_t u64 = p->data.u64;
			wmCoroutine *co;
			//解析出来fd和id
			fromuint64(u64, &fd, &id);
			co = wmCoroutine_get_by_cid(id);
			wmCoroutine_resume(co);
		}
		//有定时器才更新
		if (WorkerG.timer.num > 0) {
			//获取毫秒
			wmGetMilliTime(&mic_time);
			wmTimerWheel_update(&WorkerG.timer, mic_time);
		} else if (WorkerG.poll->event_num == 0) {
			WorkerG.is_running = false;
		}

	}
	free_wmPoll();

	return 0;
}

/**
 * 调用一个闭包函数
 */
int call_closure_func(php_fci_fcc* fci_fcc) {
	//把一些核心内容提取出来，存放在其他变量里面。
	zval _retval, *retval = &_retval;

	fci_fcc->fci.retval = retval;
	int ret = zend_call_function(&fci_fcc->fci, &fci_fcc->fcc);
	if (ret != SUCCESS) {
		php_error_docref(NULL, E_WARNING, "call onWorkerStart warning");
		return FAILURE;
	}
	if (UNEXPECTED(EG(exception))) {
		zend_exception_error(EG(exception), E_ERROR);
	}

	//释放
	zval_ptr_dtor(retval);
	return SUCCESS;
}

/**
 * 设置进程标题
 */
bool set_process_title(char* process_title) {
	zval function_name;
	zval retval_ptr;
	zval argv;

	ZVAL_STRINGL(&function_name, "cli_set_process_title", sizeof("cli_set_process_title") - 1);
	ZVAL_STRINGL(&argv, process_title, strlen(process_title));

	call_user_function(EG(function_table), NULL, &function_name, &retval_ptr, 1, &argv TSRMLS_CC);
	efree(function_name.value.str);
	efree(argv.value.str);
	if (Z_TYPE_INFO(retval_ptr) == IS_FALSE) {
		return false;
	}
	return true;
}

/**
 * 格式化字符串
 */
size_t wm_snprintf(char *buf, size_t size, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int retval = vsnprintf(buf, size, format, args);
	va_end(args);
	if (retval < 0) {
		retval = 0;
		buf[0] = '\0';
	} else if (retval >= size) {
		retval = size - 1;
		buf[retval] = '\0';
	}
	return retval;
}
