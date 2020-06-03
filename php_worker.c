/**
 * worker入口文件
 */
#include "worker.h"

zend_class_entry workerman_worker_ce;
zend_class_entry *workerman_worker_ce_ptr;

//zend_object_handlers实际上就是我们在PHP脚本上面操作一个PHP对象的时候，底层会去调用的函数。
static zend_object_handlers workerman_worker_handlers;

/**
 * 通过这个PHP对象找到我们的wmConnectionObject对象的代码
 */
wmWorkerObject* wm_worker_fetch_object(zend_object *obj) {
	return (wmWorkerObject *) ((char *) obj - workerman_worker_handlers.offset);
}

/**
 * 创建一个php对象
 * zend_class_entry是一个php类
 */
static zend_object* wmWorker_create_object(zend_class_entry *ce) {
	wmWorkerObject *worker_obj = (wmWorkerObject *) ecalloc(1,
			sizeof(wmWorkerObject) + zend_object_properties_size(ce));
	zend_object_std_init(&worker_obj->std, ce);
	object_properties_init(&worker_obj->std, ce);
	worker_obj->std.handlers = &workerman_worker_handlers;
	return &worker_obj->std;
}

/**
 * 释放php对象的方法
 * 最新 问题找到了，是因为malloc申请了的地址重复了，现在的解决办法是，这里可以释放，但是不可以关。只能在其他地方关
 */
static void wmWorker_free_object(zend_object *object) {
	wmWorkerObject *worker_obj = (wmWorkerObject *) wm_worker_fetch_object(
			object);

	wmWorker_free(worker_obj->worker);

	//free obj
	zend_object_std_dtor(&worker_obj->std);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_worker_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_worker_construct, 0, 0, 2) //
ZEND_ARG_INFO(0, listen) //
ZEND_ARG_INFO(0, options)//
ZEND_END_ARG_INFO()

PHP_METHOD(workerman_worker, __construct) {
	zval *options = NULL;
	zend_string *listen;

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_STR(listen)
				Z_PARAM_OPTIONAL
				Z_PARAM_ARRAY(options)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);
	wmWorkerObject *worker_obj = (wmWorkerObject *) wm_worker_fetch_object(
			Z_OBJ_P(getThis()));
	//初始化worker
	worker_obj->worker = wmWorker_create(getThis(), listen);

	//设置worker id
	zend_update_property_long(workerman_worker_ce_ptr, getThis(),
			ZEND_STRL("workerId"), worker_obj->worker->id);

	//解析options
	HashTable *vht = Z_ARRVAL_P(options);
	zval *ztmp = NULL;
	//backlog
	if (php_workerman_array_get_value(vht, "backlog", ztmp)) {
		zend_long v = zval_get_long(ztmp);
		worker_obj->worker->backlog = v;
		zend_update_property_long(workerman_worker_ce_ptr, getThis(),
				ZEND_STRL("backlog"), v);
	}

	//count
	worker_obj->worker->count = 1;
	if (php_workerman_array_get_value(vht, "count", ztmp)) {
		zend_long v = zval_get_long(ztmp);
		worker_obj->worker->count = v;
	}
	zend_update_property_long(workerman_worker_ce_ptr, getThis(),
			ZEND_STRL("count"), worker_obj->worker->count);

}

PHP_METHOD(workerman_worker, stop) {
	wmWorkerObject *worker_obj;
	worker_obj = (wmWorkerObject *) wm_worker_fetch_object(Z_OBJ_P(getThis()));

	if (wmWorker_stop(worker_obj->worker) == false) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

PHP_METHOD(workerman_worker, run) {
	wmWorkerObject *worker_obj = (wmWorkerObject *) wm_worker_fetch_object(
			Z_OBJ_P(getThis()));

	//php_var_dump(onWorkerStart_zval, 1 TSRMLS_CC);
	if (wmWorker_run(worker_obj->worker) == false) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

/**
 * 全部运行
 */
PHP_METHOD(workerman_worker, runAll) {
	//检查环境
	wmWorker_checkSapiEnv();

}

static const zend_function_entry workerman_worker_methods[] = { //
						PHP_ME(workerman_worker, __construct, arginfo_workerman_worker_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR) // ZEND_ACC_CTOR is used to declare that this method is a constructor of this class.
						PHP_ME(workerman_worker, run, arginfo_workerman_worker_void, ZEND_ACC_PUBLIC)
						PHP_ME(workerman_worker, runAll, arginfo_workerman_worker_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
						PHP_ME(workerman_worker, stop, arginfo_workerman_worker_void, ZEND_ACC_PUBLIC)
				PHP_FE_END };

/**
 * 注册我们的WorkerMan\Server这个类
 */
void workerman_worker_init() {
	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_worker_ce, "Corkerman", "Worker",
			workerman_worker_methods);
	//在zedn中注册类
	workerman_worker_ce_ptr = zend_register_internal_class(
			&workerman_worker_ce TSRMLS_CC); // 在 Zend Engine 中注册

	//替换掉PHP默认的handler
	memcpy(&workerman_worker_handlers, zend_get_std_object_handlers(),
			sizeof(zend_object_handlers));
	//php对象实例化已经由我们自己的代码接管了
	workerman_worker_ce_ptr->create_object = wmWorker_create_object;
	workerman_worker_handlers.free_obj = wmWorker_free_object;
	workerman_worker_handlers.offset =
			(zend_long) (((char *) (&(((wmWorkerObject*) NULL)->std)))
					- ((char *) NULL));

	//注册变量和初始值
	zend_declare_property_null(workerman_worker_ce_ptr,
			ZEND_STRL("onWorkerStart"), ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr,
			ZEND_STRL("onWorkerReload"), ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr, ZEND_STRL("onConnect"),
			ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr, ZEND_STRL("onMessage"),
	ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr, ZEND_STRL("onClose"),
	ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr,
			ZEND_STRL("onBufferFull"), ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr,
			ZEND_STRL("onBufferDrain"), ZEND_ACC_PUBLIC);
	zend_declare_property_null(workerman_worker_ce_ptr, ZEND_STRL("onError"),
	ZEND_ACC_PUBLIC);

	//静态变量
	zend_declare_property_null(workerman_worker_ce_ptr, ZEND_STRL("pidFile"),
	ZEND_ACC_PUBLIC | ZEND_ACC_STATIC);
}
