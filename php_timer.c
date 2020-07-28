/**
 * 定时器类
 */
#include "base.h"
#include "timer.h"
#include "worker.h"
#include "coroutine.h"

/**
 * 保存定時器和timer_id的关系
 */
typedef struct {
	long id;
	long cid;
	int ticks;
	bool persistent; //定时器是否循环
	wmTimerWheel_Node* timer;
	zend_fcall_info_cache fcc;
	zend_fcall_info fci;
} php_worker_timer;

static long last_id = 0;
static wmHash_INT_PTR *timers = NULL; //保存所有定时器

void php_wmTimer_init() {
	timers = wmHash_init(WM_HASH_INT_STR);
}
void php_wmTimer_shutdown() {
	wmHash_destroy(WM_HASH_INT_STR, timers);
}

//添加定时器
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_timer_add, 0, 0, 1) //
ZEND_ARG_INFO(0, seconds)
ZEND_ARG_CALLABLE_INFO(0, func, 0)
ZEND_ARG_INFO(0, args1) // 如果是数组，那么后面那个就是持久化配置。如果是持久化配置，后面那个就不解析了
ZEND_ARG_INFO(0, args2)//
ZEND_END_ARG_INFO()

//删除定时器
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_timer_resume, 0, 0, 1) //
ZEND_ARG_INFO(0, timer_id)
ZEND_END_ARG_INFO()

static void timer_free(php_worker_timer* timer) {
	if (timer->timer) {
		wmTimerWheel_del(timer->timer);
		timer->timer = NULL;
	}
	zend_fcall_info_args_clear(&timer->fci, 1);
	//引用计数-1
	wm_zend_fci_cache_discard(&timer->fcc);

	WM_HASH_DEL(WM_HASH_INT_STR, timers, timer->id);
	wm_free(timer);
}

static void timer_add_callback(void* _timer) {
	php_worker_timer* timer = (php_worker_timer*) _timer;
	timer->timer = NULL;
	timer->cid = wmCoroutine_create(&timer->fcc, timer->fci.param_count, timer->fci.params);
	if (!timer->persistent || wmWorker_getCurrent()->_status == WM_WORKER_STATUS_RELOADING) {
		timer_free(timer);
		return;
	}
	//如果不退出，那么继续循环
	timer->timer = wmTimerWheel_add_quick(&WorkerG.timer, timer_add_callback, (void*) timer, timer->ticks);
}

//协程创建实现
PHP_METHOD(workerman_timer, add) {
	double seconds;
	zval *args1 = NULL;
	zval *args2 = NULL;
	//在这里创建一个定时器
	php_worker_timer* timer = wm_malloc(sizeof(php_worker_timer));
	timer->timer = NULL;
	timer->persistent = true;
	//第一个参数表示必传的参数个数，第二个参数表示最多传入的参数个数，-1代表可变参数
	ZEND_PARSE_PARAMETERS_START(2, 4)
				Z_PARAM_DOUBLE(seconds)
				Z_PARAM_FUNC(timer->fci, timer->fcc)
				Z_PARAM_OPTIONAL
				Z_PARAM_ZVAL(args1)
				Z_PARAM_ZVAL(args2)
			ZEND_PARSE_PARAMETERS_END_EX(efree(timer); RETURN_FALSE);
	if (UNEXPECTED(seconds < 0.001)) {
		php_error_docref(NULL, E_WARNING, "Timer must be greater than or equal to 0.001");
		RETURN_FALSE
	}
	//如果第一个参数数数组的话
	if (args1) {
		if (Z_TYPE_P(args1) == IS_ARRAY) {
			if (args2 && Z_TYPE_P(args2) == IS_FALSE) {
				timer->persistent = false;
			}
			//在这里解析数组
			zend_fcall_info_args(&timer->fci, args1);
			timer->fci.retval = NULL;
		} else if (Z_TYPE_P(args1) == IS_FALSE) {
			timer->persistent = false;
		}
	}
	timer->id = ++last_id;
	timer->ticks = seconds * 1000;

	//fcc的引用计数+1
	wm_zend_fci_cache_persist(&timer->fcc);

	if (WM_HASH_ADD(WM_HASH_INT_STR, timers, timer->id,timer) < 0) {
		wmWarn("workerman_timer_add-> fail");
		timer_free(timer);
		RETURN_FALSE
	}
	timer->timer = wmTimerWheel_add_quick(&WorkerG.timer, timer_add_callback, (void*) timer, timer->ticks);
	RETURN_LONG(timer->id)
}

//删除定时器
PHP_METHOD(workerman_timer, del) {
	zend_long timer_id = 0;
	ZEND_PARSE_PARAMETERS_START(1, 1)
				Z_PARAM_LONG(timer_id)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	php_worker_timer* timer = WM_HASH_GET(WM_HASH_INT_STR, timers, timer_id);
	if (!timer) {
		wmWarn("timer_id not exist");
		RETURN_FALSE
	}
	timer_free(timer);
	RETURN_TRUE
}

const zend_function_entry workerman_timer_methods[] = { //
	PHP_ME(workerman_timer, add, arginfo_workerman_timer_add, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
		PHP_ME(workerman_timer, del, arginfo_workerman_timer_resume, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
		PHP_FE_END //
		};

/**
 * 定义 zend class entry
 */
zend_class_entry workerman_timer_ce;
zend_class_entry *workerman_timer_ce_ptr;

/**
 * 注册我们的Warriorman\Timer这个类
 */
void workerman_timer_init() {
	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_timer_ce, "Warriorman", "Lib\\Timer", workerman_timer_methods);
	//在zedn中注册类
	workerman_timer_ce_ptr = zend_register_internal_class(&workerman_timer_ce TSRMLS_CC); // 在 Zend Engine 中注册
}
