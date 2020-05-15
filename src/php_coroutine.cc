/**
 * 协程工具入口文件
 */
#include "coroutine.h"

using workerman::WorkerCoroutine;
using workerman::Coroutine;

static swHashMap *user_yield_coros = swHashMap_new(NULL);

//声明方法
static PHP_METHOD(workerman_coroutine, yield);
static PHP_METHOD(workerman_coroutine, resume);
static PHP_METHOD(workerman_coroutine, getCid);
static PHP_METHOD(workerman_coroutine, defer);
static PHP_METHOD(workerman_coroutine, sleep);

//创建协程接口参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_create, 0, 0, 1) //
ZEND_ARG_CALLABLE_INFO(0, func, 0)
ZEND_END_ARG_INFO()

//不需要参数的方法用
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_void, 0, 0, 0)	//
ZEND_END_ARG_INFO()

//传入协程resume参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_resume, 0, 0, 1) //
ZEND_ARG_INFO(0, cid)
ZEND_END_ARG_INFO()

//判断协程是否存在
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_isExist, 0, 0, 1) //
ZEND_ARG_INFO(0, cid)
ZEND_END_ARG_INFO()

//defer
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_defer, 0, 0, 1) //
ZEND_ARG_CALLABLE_INFO(0, func, 0)
ZEND_END_ARG_INFO()

//sleep
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_sleep, 0, 0, 1) ZEND_ARG_INFO(0, seconds)
ZEND_END_ARG_INFO()

//协程创建实现
PHP_FUNCTION(workerman_coroutine_create) {
	zend_fcall_info fci = empty_fcall_info;
	zend_fcall_info_cache fcc = empty_fcall_info_cache;
	//zval result;

	//第一个参数表示必传的参数个数，第二个参数表示最多传入的参数个数，-1代表可变参数
	ZEND_PARSE_PARAMETERS_START(1, -1)
				Z_PARAM_FUNC(fci, fcc)
				//第一个参数是一个匿名方法
				// Z_PARAM_VARIADIC这个宏是用来解析可变参数的，'*'对于Z_PARAM_VARIADIC实际上并没有用到。
				// 接下来获取其他参数。* 表示可变参数可传或者不传递。与之对应的是'+'，表示可变参数至少传递一个。
				// 这个宏设置fci.params指针的起始位置，以及fci.param_count的值
				Z_PARAM_VARIADIC('*', fci.params, fci.param_count)			//
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE); //如果不满足传入条件，返回false
	//函数的返回值，放入result
	//fci.retval = &result;
	//运行传入的函数
	//	if (zend_call_function(&fci, &fcc) != SUCCESS) {
	//		return;
	//	}
	//	*return_value = result;
	long cid = WorkerCoroutine::create(&fcc, fci.param_count, fci.params);
	RETURN_LONG(cid);
}

//协程yield
PHP_METHOD(workerman_coroutine, yield) {
	Coroutine* co = Coroutine::get_current();

	swHashMap_add_int(user_yield_coros, co->get_cid(), co);

	co->yield();
	RETURN_TRUE
}

//协程resume
PHP_METHOD(workerman_coroutine, resume) {
	zend_long cid = 0;

	ZEND_PARSE_PARAMETERS_START(1, 1)
				Z_PARAM_LONG(cid)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	Coroutine* co = (Coroutine*) swHashMap_find_int(user_yield_coros, cid);
	if (co == NULL) {
		php_error_docref(NULL, E_WARNING, "resume error");
		RETURN_FALSE
	}
	swHashMap_del_int(user_yield_coros, cid);

	co->resume();
	RETURN_TRUE
}

//获取协程cid
PHP_METHOD(workerman_coroutine, getCid) {
	Coroutine* co = Coroutine::get_current();
	if (co == nullptr) {
		RETURN_LONG(-1);
	}
	RETURN_LONG(co->get_cid());
}

//判断协程是否存在
PHP_METHOD(workerman_coroutine, isExist) {
	zend_long cid = 0;
	bool is_exist;

	ZEND_PARSE_PARAMETERS_START(1, 1)
				Z_PARAM_LONG(cid)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	Coroutine* co = (Coroutine*) swHashMap_find_int(Coroutine::coroutines, cid);
	is_exist = (co != NULL);

	RETURN_BOOL(is_exist);
}

PHP_METHOD(workerman_coroutine, defer) {
	zend_fcall_info fci = empty_fcall_info;
	zend_fcall_info_cache fcc = empty_fcall_info_cache;
	php_fci_fcc *defer_fci_fcc;

	defer_fci_fcc = (php_fci_fcc *) emalloc(sizeof(php_fci_fcc));

	ZEND_PARSE_PARAMETERS_START(1, -1)
				Z_PARAM_FUNC(fci, fcc)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	defer_fci_fcc->fci = fci;
	defer_fci_fcc->fcc = fcc;

	WorkerCoroutine::defer(defer_fci_fcc);
}

/**
 * sleep方法
 */
PHP_METHOD(workerman_coroutine, sleep) {
	double seconds;

	ZEND_PARSE_PARAMETERS_START(1, 1)
				Z_PARAM_DOUBLE(seconds)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	if (UNEXPECTED(seconds < 0.001)) {
		php_error_docref(NULL, E_WARNING,
				"Timer must be greater than or equal to 0.001");
		RETURN_FALSE
	}

	Coroutine* co = Coroutine::get_current();
	timernode_t node1;
	timerwheel_node_init(&node1, WorkerCoroutine::sleep, (void*) co);
	timerwheel_add(&WorkerG.timer, &node1, seconds * 1000);

	co->yield();
	RETURN_TRUE
}

/**
 * 我们需要对这个方法进行收集，放在变量 workerman_coroutine_methods里面
 */
const zend_function_entry workerman_coroutine_methods[] = { //
				//PHP_ME(workerman_coroutine, create, arginfo_workerman_coroutine_create, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
				ZEND_FENTRY(create, ZEND_FN(workerman_coroutine_create),
						arginfo_workerman_coroutine_create,
						ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) // ZEND_FENTRY这行是新增的
						PHP_ME(workerman_coroutine, yield, arginfo_workerman_coroutine_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
						PHP_ME(workerman_coroutine, resume, arginfo_workerman_coroutine_resume, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
						PHP_ME(workerman_coroutine, getCid, arginfo_workerman_coroutine_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
						PHP_ME(workerman_coroutine, isExist, arginfo_workerman_coroutine_isExist, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
						PHP_ME(workerman_coroutine, defer, arginfo_workerman_coroutine_defer, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
						PHP_ME(workerman_coroutine, sleep, arginfo_workerman_coroutine_sleep, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC) //
				PHP_FE_END //
				};

/**
 * 定义 zend class entry
 */
zend_class_entry workerman_coroutine_ce;
zend_class_entry *workerman_coroutine_ce_ptr;

/**
 * 注册我们的WorkerMan\Coroutine这个类
 * 考虑到以后我们会有许多的类，我们不在MINIT里面直接写注册的代码，而是让study_coroutine_util.cc提供一个函数，我们在这个函数里面实现注册功能：
 */
void workerman_coroutine_init() {
	WorkerCoroutine::init();
//定义好一个类
	INIT_CLASS_ENTRY(workerman_coroutine_ce, "Workerman",
			workerman_coroutine_methods);
//在zedn中注册类
	workerman_coroutine_ce_ptr = zend_register_internal_class(
			&workerman_coroutine_ce TSRMLS_CC); // 在 Zend Engine 中注册

	//短名
	zend_register_class_alias("worker_coroutine", workerman_coroutine_ce_ptr);
}
