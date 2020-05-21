/**
 * server入口文件
 */
#include "bash.h"
#include "coroutine_socket.h"

zend_class_entry workerman_socket_ce;
zend_class_entry *workerman_socket_ce_ptr;

//为了通过php对象，找到上面的c++对象
typedef struct {
	wmCoroutionSocket *socket; //c对象 这个是create产生的
	zend_object std; //php对象
} wmCoroutionSocketObject;

//zend_object_handlers实际上就是我们在PHP脚本上面操作一个PHP对象的时候，底层会去调用的函数。
static zend_object_handlers workerman_socket_handlers;

/**
 * 通过这个PHP对象找到我们的wmCoroutionSocketObject对象的代码
 */
static wmCoroutionSocketObject* wm_socket_fetch_object(zend_object *obj) {
	return (wmCoroutionSocketObject *) ((char *) obj
			- workerman_socket_handlers.offset);
}

/**
 * 创建一个php对象
 * zend_class_entry是一个php类
 */
static zend_object* wm_socket_create_object(zend_class_entry *ce) {
	wmCoroutionSocketObject *sock = (wmCoroutionSocketObject *) ecalloc(1,
			sizeof(wmCoroutionSocketObject) + zend_object_properties_size(ce));
	zend_object_std_init(&sock->std, ce);
	object_properties_init(&sock->std, ce);
	sock->std.handlers = &workerman_socket_handlers;
	return &sock->std;
}

/**
 * 释放php对象的方法
 * 最新 问题找到了，是因为malloc申请了的地址重复了，现在的解决办法是，这里可以释放，但是不可以关。只能在其他地方关
 */
static void wm_socket_free_object(zend_object *object) {
	wmCoroutionSocketObject *sock =
			(wmCoroutionSocketObject *) wm_socket_fetch_object(object);

	//这里需要判断，这个socket是不是被人继续用了。
	if (sock->socket && sock->socket != NULL) {
		//现在有这个时候，把别的正常的fd关闭的情况
		wmCoroutionSocket_free(sock->socket);
	}
	//free_obj
	zend_object_std_dtor(&sock->std);
}

//初始化一个自定义的PHP对象，并且让zsocket这个容器指向自定义对象里面的std对象
void php_wm_init_socket_object(zval *zsocket, wmCoroutionSocket *socket) {
	zend_object *object = wm_socket_create_object(workerman_socket_ce_ptr);

	wmCoroutionSocketObject *socket_t =
			(wmCoroutionSocketObject *) wm_socket_fetch_object(object);
	socket_t->socket = socket;
	ZVAL_OBJ(zsocket, object);
}

//构造函数
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_socket_construct, 0, 0, 2) //
ZEND_ARG_INFO(0, domain)
ZEND_ARG_INFO(0, type)
ZEND_ARG_INFO(0, protocol)
ZEND_END_ARG_INFO()

//bind
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_socket_bind, 0, 0, 2) //
ZEND_ARG_INFO(0, host)
ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

//listen
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_socket_listen, 0, 0, 0) //
ZEND_ARG_INFO(0, backlog)
ZEND_END_ARG_INFO()

//空的参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_socket_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

//接收数据recv
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_socket_recv, 0, 0, 0) //
ZEND_ARG_INFO(0, length)
ZEND_END_ARG_INFO()

//发送数据方法
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_socket_send, 0, 0, 2) //
ZEND_ARG_INFO(0, data)
ZEND_ARG_INFO(0, fd)
ZEND_END_ARG_INFO()

PHP_METHOD(workerman_socket, __construct) {
	wmCoroutionSocketObject* socket_object;
	zend_long domain;
	zend_long type;
	zend_long protocol = 0;

	//声明参数获取
	ZEND_PARSE_PARAMETERS_START(2, 3)
				Z_PARAM_LONG(domain)
				Z_PARAM_LONG(type)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(protocol)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));
	socket_object->socket = wmCoroutionSocket_init(domain, type, protocol);

	zend_update_property_long(workerman_socket_ce_ptr, getThis(),
			ZEND_STRL("fd"), socket_object->socket->sockfd);
}

PHP_METHOD(workerman_socket, bind) {
	wmCoroutionSocketObject* socket_object;
	zval *zhost;
	zend_long zport;

	//声明参数获取
	ZEND_PARSE_PARAMETERS_START(2, 2)
				Z_PARAM_ZVAL(zhost)
				Z_PARAM_LONG(zport)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));

	if (wmCoroutionSocket_bind(socket_object->socket, Z_STRVAL_P(zhost),
			zport) < 0) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

PHP_METHOD(workerman_socket, listen) {
	wmCoroutionSocketObject* socket_object;
	zend_long backlog = 512;

	//声明参数获取
	ZEND_PARSE_PARAMETERS_START(0, 1)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(backlog)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));

	if (wmCoroutionSocket_listen(socket_object->socket, backlog) < 0) {
		RETURN_FALSE
	}
	RETURN_TRUE
}

/**
 * 获取客户端连接,这里返回一个自定义的object
 */
PHP_METHOD(workerman_socket, accept) {
	wmCoroutionSocketObject* socket_object;
	wmCoroutionSocketObject* socket_object2;
	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));

	//新的Socket对象
	zend_object *obj = wm_socket_create_object(workerman_socket_ce_ptr);
	socket_object2 = (wmCoroutionSocketObject *) wm_socket_fetch_object(obj);

	//接客
	socket_object2->socket = wmCoroutionSocket_accept(socket_object->socket);

//	zval obj_zval;
//	ZVAL_OBJ(&obj_zval, &(socket_object2->std));
//	zend_update_property_long(workerman_socket_ce_ptr, &obj_zval,
//				ZEND_STRL("fd"), socket_object2->socket->sockfd);
//	*return_value = obj_zval;

	ZVAL_OBJ(return_value, &(socket_object2->std));
	zend_update_property_long(workerman_socket_ce_ptr, return_value,
			ZEND_STRL("fd"), socket_object2->socket->sockfd);
}

/**
 * 接收数据
 */
PHP_METHOD(workerman_socket, recv) {
	wmCoroutionSocketObject* socket_object;
	wmCoroutionSocket *conn;
	ssize_t ret;
	zend_long length = WM_BUFFER_SIZE_BIG;

	ZEND_PARSE_PARAMETERS_START(0, 1)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(length)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));
	conn = socket_object->socket;

	if (conn == NULL) {
		php_error_docref(NULL, E_WARNING, "recv error");
		RETURN_FALSE
	}

	wmCoroutionSocket_recv(conn, length);
	ret = conn->read_buffer->length;

	//客户端已关闭
	if (ret == 0) {
		zend_update_property_long(workerman_socket_ce_ptr, getThis(),
				ZEND_STRL("errCode"), WM_ERROR_SESSION_CLOSED_BY_CLIENT);

		zend_update_property_string(workerman_socket_ce_ptr,
		getThis(), ZEND_STRL("errMsg"),
				wmCode_str(WM_ERROR_SESSION_CLOSED_BY_CLIENT));
		wmCoroutionSocket_close(conn);
		RETURN_FALSE
	}
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "recv error");
		RETURN_FALSE
	}

	//计数重新开始
	conn->read_buffer->length = 0;

	RETURN_STRINGL(conn->read_buffer->str, ret);
	//conn->read_buffer->str[ret] = '\0';
	//RETURN_STRING(conn->read_buffer->str);
}

//发送数据
PHP_METHOD(workerman_socket, send) {
	wmCoroutionSocketObject* socket_object;
	wmCoroutionSocket *conn;

	ssize_t ret;
	zend_long fd = 0;
	char *data;
	size_t length;

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_STRING(data, length)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(fd)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));
	conn = socket_object->socket;
	if (conn == NULL) {
		php_error_docref(NULL, E_WARNING, "send error");
		RETURN_FALSE
	}

	ret = wmCoroutionSocket_send(conn, data, length);
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "send error");
		//释放掉申请的内存
		wmCoroutionSocket_close(conn);
		RETURN_FALSE
	}
	RETURN_LONG(ret);
}

PHP_METHOD(workerman_socket, close) {
	wmCoroutionSocketObject* socket_object;
	int ret = 0;
	socket_object = (wmCoroutionSocketObject *) wm_socket_fetch_object(
			Z_OBJ_P(getThis()));
	ret = wmCoroutionSocket_close(socket_object->socket);
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "close error");
		RETURN_FALSE
	}
	RETURN_LONG(ret);
}

static const zend_function_entry workerman_socket_methods[] = { //
						PHP_ME(workerman_socket, __construct, arginfo_workerman_socket_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR) //
						PHP_ME(workerman_socket, bind, arginfo_workerman_socket_bind, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_socket, listen, arginfo_workerman_socket_listen, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_socket, accept, arginfo_workerman_socket_void, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_socket, recv, arginfo_workerman_socket_recv, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_socket, send, arginfo_workerman_socket_send, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_socket, close, arginfo_workerman_socket_void, ZEND_ACC_PUBLIC) //
				PHP_FE_END };

/**
 * 注册我们的WorkerMan\Server这个类
 */
void workerman_socket_init() {

	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_socket_ce, "Workerman", "Socket",
			workerman_socket_methods);
	//在zedn中注册类
	workerman_socket_ce_ptr = zend_register_internal_class(
			&workerman_socket_ce TSRMLS_CC); // 在 Zend Engine 中注册

	//短名
	zend_register_class_alias("worker_socket", workerman_socket_ce_ptr);

	//替换掉PHP默认的handler
	memcpy(&workerman_socket_handlers, zend_get_std_object_handlers(),
			sizeof(zend_object_handlers));
	//php对象实例化已经由我们自己的代码接管了
	workerman_socket_ce_ptr->create_object = wm_socket_create_object;
	workerman_socket_handlers.free_obj = wm_socket_free_object;
	workerman_socket_handlers.offset =
			(zend_long) (((char *) (&(((wmCoroutionSocketObject*) NULL)->std)))
					- ((char *) NULL));

	//类进行初始化的时候设置变量
	zend_declare_property_long(workerman_socket_ce_ptr, ZEND_STRL("fd"), 0,
	ZEND_ACC_PUBLIC);

	//注册变量和初始值
	zend_declare_property_long(workerman_socket_ce_ptr, ZEND_STRL("errCode"), 0,
	ZEND_ACC_PUBLIC);
	zend_declare_property_string(workerman_socket_ce_ptr, ZEND_STRL("errMsg"),
			"", ZEND_ACC_PUBLIC);

}
