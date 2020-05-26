/**
 * server入口文件
 */
#include "worker_server.h"

zend_class_entry workerman_server_ce;
zend_class_entry *workerman_server_ce_ptr;

//为了通过php对象，找到上面的c++对象
typedef struct {
	wmServer *server; //c对象 这个是create产生的
	zend_object std; //php对象
} wmServerObject;

//zend_object_handlers实际上就是我们在PHP脚本上面操作一个PHP对象的时候，底层会去调用的函数。
static zend_object_handlers workerman_server_handlers;

/**
 * 通过这个PHP对象找到我们的wmCoroutionSocketObject对象的代码
 */
static wmServerObject* wm_server_fetch_object(zend_object *obj) {
	return (wmServerObject *) ((char *) obj - workerman_server_handlers.offset);
}

/**
 * 创建一个php对象
 * zend_class_entry是一个php类
 */
static zend_object* wmServer_create_object(zend_class_entry *ce) {
	wmServerObject *server_obj = (wmServerObject *) ecalloc(1,
			sizeof(wmServerObject) + zend_object_properties_size(ce));
	zend_object_std_init(&server_obj->std, ce);
	object_properties_init(&server_obj->std, ce);
	server_obj->std.handlers = &workerman_server_handlers;
	return &server_obj->std;
}

/**
 * 释放php对象的方法
 * 最新 问题找到了，是因为malloc申请了的地址重复了，现在的解决办法是，这里可以释放，但是不可以关。只能在其他地方关
 */
static void wmServer_free_object(zend_object *object) {
	wmServerObject *server_obj = (wmServerObject *) wm_server_fetch_object(
			object);

	wmServer_free(server_obj->server);

	//free obj
	zend_object_std_dtor(&server_obj->std);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_construct, 0, 0, 2) //
ZEND_ARG_INFO(0, listen) //
ZEND_ARG_INFO(0, options)//
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_handler, 0, 0, 1) //
ZEND_ARG_CALLABLE_INFO(0, func, 0) //
ZEND_END_ARG_INFO()

PHP_METHOD(workerman_server, __construct) {
	zval *options = NULL;
	zend_string *listen;

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_STR(listen)
				Z_PARAM_OPTIONAL
				Z_PARAM_ARRAY(options)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);
	//初始化server
	wmServerObject *server_obj = (wmServerObject *) wm_server_fetch_object(
			Z_OBJ_P(getThis()));
	server_obj->server = wmServer_create2("127.0.0.1", 8080);
	//设置worker id
	zend_update_property_long(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("workerId"), server_obj->server->id);

	//解析options
	HashTable *vht = Z_ARRVAL_P(options);
	zval *ztmp = NULL;
	//backlog
	if (php_workerman_array_get_value(vht, "backlog", ztmp)) {
		zend_long v = zval_get_long(ztmp);
		server_obj->server->backlog = v;
		zend_update_property_long(workerman_server_ce_ptr, getThis(),
				ZEND_STRL("backlog"), v);
	}

	//count
	server_obj->server->count = 1;
	if (php_workerman_array_get_value(vht, "count", ztmp)) {
		zend_long v = zval_get_long(ztmp);
		server_obj->server->count = v;
	}
	zend_update_property_long(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("count"), server_obj->server->count);

}

PHP_METHOD(workerman_server, stop) {
	wmServerObject *server_obj;
	server_obj = (wmServerObject *) wm_server_fetch_object(Z_OBJ_P(getThis()));

	if (wmServer_stop(server_obj->server) == false) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

PHP_METHOD(workerman_server, set_handler) {
	wmServerObject *server_obj;
	php_fci_fcc *handle_fci_fcc;

	handle_fci_fcc = (php_fci_fcc *) ecalloc(1, sizeof(php_fci_fcc));

	ZEND_PARSE_PARAMETERS_START(1, 1)
				Z_PARAM_FUNC(handle_fci_fcc->fci, handle_fci_fcc->fcc)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	//引用计数+1
	//Z_TRY_ADDREF_P(zdata);
	server_obj = (wmServerObject *) wm_server_fetch_object(Z_OBJ_P(getThis()));

	//增加引用计数
	wm_zend_fci_cache_persist(&handle_fci_fcc->fcc);

	wmServer_set_handler(server_obj->server, handle_fci_fcc);
}

PHP_METHOD(workerman_server, run) {
	wmServerObject *server_obj;
	server_obj = (wmServerObject *) wm_server_fetch_object(Z_OBJ_P(getThis()));
	//php_var_dump(onWorkerStart_zval, 1 TSRMLS_CC);

	//判断是否有workerStart
	zval *_zval = wm_zend_read_property(workerman_server_ce_ptr,
	getThis(), ZEND_STRL("onWorkerStart"), 0);
	php_fci_fcc *handle_fci_fcc = (php_fci_fcc *) ecalloc(1,
			sizeof(php_fci_fcc));

	char *_error = NULL;
	if (!_zval
			|| zend_parse_arg_func(_zval, &handle_fci_fcc->fci,
					&handle_fci_fcc->fcc, 0, &_error) == 0) {
		efree(handle_fci_fcc);
		php_error_docref(NULL, E_ERROR, "onWorkerStart error : %s" , _error);
		RETURN_FALSE
	}


	//为这个闭包增加引用计数
	wm_zend_fci_cache_persist(&handle_fci_fcc->fcc);
	server_obj->server->onWorkerStart = handle_fci_fcc;
	server_obj->server->onWorkerStart->fci.param_count = 1;
	server_obj->server->onWorkerStart->fci.params = getThis();

	if (wmServer_run(server_obj->server) == false) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

static const zend_function_entry workerman_server_methods[] = { //
						PHP_ME(workerman_server, __construct, arginfo_workerman_server_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR) // ZEND_ACC_CTOR is used to declare that this method is a constructor of this class.
						PHP_ME(workerman_server, run, arginfo_workerman_server_void, ZEND_ACC_PUBLIC)
						PHP_ME(workerman_server, stop, arginfo_workerman_server_void, ZEND_ACC_PUBLIC)
						PHP_ME(workerman_server, set_handler, arginfo_workerman_server_handler, ZEND_ACC_PUBLIC)
				PHP_FE_END };

/**
 * 注册我们的WorkerMan\Server这个类
 */
void workerman_server_init() {

	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_server_ce, "Workerman", "Server",
			workerman_server_methods);
	//在zedn中注册类
	workerman_server_ce_ptr = zend_register_internal_class(
			&workerman_server_ce TSRMLS_CC); // 在 Zend Engine 中注册

	//短名
	zend_register_class_alias("worker_server", workerman_server_ce_ptr);

	//替换掉PHP默认的handler
	memcpy(&workerman_server_handlers, zend_get_std_object_handlers(),
			sizeof(zend_object_handlers));
	//php对象实例化已经由我们自己的代码接管了
	workerman_server_ce_ptr->create_object = wmServer_create_object;
	workerman_server_handlers.free_obj = wmServer_free_object;
	workerman_server_handlers.offset =
			(zend_long) (((char *) (&(((wmServerObject*) NULL)->std)))
					- ((char *) NULL));

	//注册变量和初始值
	zend_declare_property_long(workerman_server_ce_ptr, ZEND_STRL("errCode"), 0,
	ZEND_ACC_PUBLIC);
	zend_declare_property_string(workerman_server_ce_ptr, ZEND_STRL("errMsg"),
			"",
			ZEND_ACC_PUBLIC);

}
