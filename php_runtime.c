/**
 * hook一些php的函数
 */
#include "runtime.h"

extern PHP_METHOD(workerman_coroutine, sleep);

zend_class_entry workerman_runtime_ce;
zend_class_entry *workerman_runtime_ce_ptr;

static void hook_func(const char *name, size_t name_len, zif_handler new_handler) {
	zend_function *ori_f = (zend_function *) zend_hash_str_find_ptr(EG(function_table), name, name_len);
	ori_f->internal_function.handler = new_handler;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_runtime_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

//开启协程模式,hook相关函数
void wm_enableCoroutine() {
	hook_func(ZEND_STRL("sleep"), zim_workerman_coroutine_sleep);
	php_stream_xport_register("tcp", wmRuntime_socket_create); // 使用socket_create这个函数，替换原来的php_stream_generic_socket_factory
}

PHP_METHOD(workerman_runtime, enableCoroutine) {
	wm_enableCoroutine();
}

static const zend_function_entry workerman_runtime_methods[] = { //
	PHP_ME(workerman_runtime, enableCoroutine, arginfo_workerman_runtime_void, ZEND_ACC_PUBLIC| ZEND_ACC_STATIC)
	PHP_FE_END //
		};

/**
 * 注册我们的WorkerMan\Server这个类
 */
void workerman_runtime_init() {

	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_runtime_ce, "Warriorman", "Runtime", workerman_runtime_methods);
	//在zedn中注册类
	workerman_runtime_ce_ptr = zend_register_internal_class(&workerman_runtime_ce TSRMLS_CC); // 在 Zend Engine 中注册
}
