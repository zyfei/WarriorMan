/**
 * worker入口文件
 */
#include "bash.h"
#include "connection.h"

zend_class_entry workerman_connection_ce;
zend_class_entry *workerman_connection_ce_ptr;

//zend_object_handlers实际上就是我们在PHP脚本上面操作一个PHP对象的时候，底层会去调用的函数。
static zend_object_handlers workerman_connection_handlers;

/**
 * 通过这个PHP对象找到我们的wmConnectionObject对象的代码
 */
wmConnectionObject* wm_connection_fetch_object(zend_object *obj) {
	return (wmConnectionObject *) ((char *) obj
			- workerman_connection_handlers.offset);
}

/**
 * 创建一个php对象
 * zend_class_entry是一个php类
 */
zend_object* wm_connection_create_object(zend_class_entry *ce) {
	wmConnectionObject *sock = (wmConnectionObject *) ecalloc(1,
			sizeof(wmConnectionObject) + zend_object_properties_size(ce));
	zend_object_std_init(&sock->std, ce);
	object_properties_init(&sock->std, ce);
	sock->std.handlers = &workerman_connection_handlers;
	return &sock->std;
}

/**
 * 释放php对象的方法
 * 最新 问题找到了，是因为malloc申请了的地址重复了，现在的解决办法是，这里可以释放，但是不可以关。只能在其他地方关
 */
static void wm_connection_free_object(zend_object *object) {
	wmConnectionObject *sock =
			(wmConnectionObject *) wm_connection_fetch_object(object);

	//这里需要判断，这个connection是不是被人继续用了。
	if (sock->connection && sock->connection != NULL) {
		//现在有这个时候，把别的正常的fd关闭的情况
		wmConnection_free(sock->connection);
	}
	//free_obj
	zend_object_std_dtor(&sock->std);
}

//空的参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_connection_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

//发送数据方法
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_connection_send, 0, 0, 2) //
ZEND_ARG_INFO(0, data)
ZEND_ARG_INFO(0, fd)
ZEND_END_ARG_INFO()

//发送数据
PHP_METHOD(workerman_connection, send) {
	wmConnectionObject* connection_object;
	wmConnection *conn;

	zend_long fd = 0;
	char *data;
	size_t length;

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_STRING(data, length)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(fd)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	connection_object = (wmConnectionObject *) wm_connection_fetch_object(
			Z_OBJ_P(getThis()));
	conn = connection_object->connection;
	if (conn == NULL) {
		php_error_docref(NULL, E_WARNING, "send error");
		RETURN_FALSE
	}

	if (!wmConnection_send(conn, data, length)) {
		php_error_docref(NULL, E_WARNING, "send error");
		RETURN_FALSE
	}
	RETURN_TRUE
}

PHP_METHOD(workerman_connection, close) {
	wmConnectionObject* connection_object;
	int ret = 0;
	connection_object = (wmConnectionObject *) wm_connection_fetch_object(
			Z_OBJ_P(getThis()));
	ret = wmConnection_close(connection_object->connection);
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "close error");
		RETURN_FALSE
	}
	RETURN_LONG(ret);
}

static const zend_function_entry workerman_connection_methods[] = { //
						PHP_ME(workerman_connection, send, arginfo_workerman_connection_send, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_connection, close, arginfo_workerman_connection_void, ZEND_ACC_PUBLIC) //
				PHP_FE_END };

/**
 * 注册我们的WorkerMan\Server这个类
 */
void workerman_connection_init() {

	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_connection_ce, "Workerman", "Connection",
			workerman_connection_methods);
	//在zedn中注册类
	workerman_connection_ce_ptr = zend_register_internal_class(
			&workerman_connection_ce TSRMLS_CC); // 在 Zend Engine 中注册

	//替换掉PHP默认的handler
	memcpy(&workerman_connection_handlers, zend_get_std_object_handlers(),
			sizeof(zend_object_handlers));
	//php对象实例化已经由我们自己的代码接管了
	workerman_connection_ce_ptr->create_object = wm_connection_create_object;
	workerman_connection_handlers.free_obj = wm_connection_free_object;
	workerman_connection_handlers.offset =
			(zend_long) (((char *) (&(((wmConnectionObject*) NULL)->std)))
					- ((char *) NULL));

	//类进行初始化的时候设置变量
	zend_declare_property_long(workerman_connection_ce_ptr, ZEND_STRL("fd"), 0,
	ZEND_ACC_PUBLIC);

	//注册变量和初始值
	zend_declare_property_long(workerman_connection_ce_ptr,
			ZEND_STRL("errCode"), 0,
			ZEND_ACC_PUBLIC);
	zend_declare_property_string(workerman_connection_ce_ptr,
			ZEND_STRL("errMsg"), "", ZEND_ACC_PUBLIC);

}
