/* workerman extension for PHP */
#include "workerman.h"
#include "base.h"
#include "worker.h"

//创建协程接口方法声明
PHP_FUNCTION(workerman_coroutine_create);

//空的参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

//创建协程接口参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_coroutine_create, 0, 0, 1) //
ZEND_ARG_CALLABLE_INFO(0, func, 0)
ZEND_END_ARG_INFO()

/**
 * 模块初始化阶段
 * 这个阶段不要使用php内存管理，比如emalloc申请内存。
 */
PHP_MINIT_FUNCTION(workerman) {
	//初始化协程定义
	workerman_coroutine_init();
	//初始化connection
	workerman_connection_init();
	//初始化worker相关
	workerman_worker_init();
	//初始化channel
	workerman_channel_init();
	//初始化runtime
	workerman_runtime_init();
	return SUCCESS;
}

/**
 * 模块关闭阶段
 */
PHP_MSHUTDOWN_FUNCTION(workerman) {
	return SUCCESS;
}

/**
 * 请求初始化阶段
 */
PHP_RINIT_FUNCTION(workerman) {
	//初始化base相关
	workerman_base_init();
	wmCoroutine_init();
	return SUCCESS;
}

/**
 * 请求关闭阶段
 */
PHP_RSHUTDOWN_FUNCTION(workerman) {
	workerman_base_shutdown();
	wmCoroutine_shutdown();
	return SUCCESS;
}

/**
 * phpinfo相关信息
 */
PHP_MINFO_FUNCTION(workerman) {
	php_info_print_table_start();
	php_info_print_table_header(2, "workerman support", "enabled");
	php_info_print_table_end();
}

PHP_FUNCTION(worker_event_wait) {
	int ret;
	ret = wm_event_wait();
	if (ret < 0) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

static const zend_function_entry workerman_functions[] = { //
	PHP_FE(workerman_coroutine_create, arginfo_workerman_coroutine_create) //
		PHP_FALIAS(worker_go, workerman_coroutine_create, arginfo_workerman_coroutine_create) //
		PHP_FE(worker_event_wait, arginfo_workerman_void) //
		PHP_FE_END };

zend_module_entry workerman_module_entry = {
STANDARD_MODULE_HEADER, "workerman", //
	workerman_functions, //
	PHP_MINIT(workerman),
	PHP_MSHUTDOWN(workerman),
	PHP_RINIT(workerman),
	PHP_RSHUTDOWN(workerman),
	PHP_MINFO(workerman),
	PHP_WORKERMAN_VERSION, //
	STANDARD_MODULE_PROPERTIES //
	};

#ifdef COMPILE_DL_WORKERMAN
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(workerman)
#endif

