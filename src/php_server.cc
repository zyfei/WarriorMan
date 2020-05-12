/**
 * server入口文件
 */
#include "workerman.h"
#include "server.h"

PHP_METHOD(workerman_server, __construct);
PHP_METHOD(workerman_server, accept);
PHP_METHOD(workerman_server, recv);
PHP_METHOD(workerman_server, send);

//构造函数
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_construct, 0, 0, 2) //
ZEND_ARG_INFO(0, host)
ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

//空的参数声明
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

//接收数据recv
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_recv, 0, 0, 2) //
ZEND_ARG_INFO(0, fd)
ZEND_ARG_INFO(0, length)
ZEND_END_ARG_INFO()

//发送数据方法
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_send, 0, 0, 2) //
ZEND_ARG_INFO(0, fd)
ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

PHP_METHOD(workerman_server, __construct) {
	int sock;
	zval *zhost;
	zend_long zport;
	//声明参数获取
	ZEND_PARSE_PARAMETERS_START(2, 2)
				Z_PARAM_ZVAL(zhost)
				Z_PARAM_LONG(zport)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);
	//创建套接字
	sock = wmSocket_create(WM_SOCK_TCP);
	//bind套接字
	wmSocket_bind(sock, WM_SOCK_TCP, Z_STRVAL_P(zhost), zport);
	wmSocket_listen(sock); // 修改的地方

	//然后把sock、host、port保存下来作为server对象的属性。
	zend_update_property_long(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("sock"), sock);
	zend_update_property_string(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("host"), Z_STRVAL_P(zhost));
	zend_update_property_long(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("port"), zport);
}

/**
 * 获取客户端连接
 */
PHP_METHOD(workerman_server, accept) {
	zval *zsock;
	int connfd;
	//读取socket,最后0是采用默认模式，如果没有只警告
	zsock = wm_zend_read_property(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("sock"), 0);
	connfd = wmSocket_accept(Z_LVAL_P(zsock));
	RETURN_LONG(connfd);
}

/**
 * 接收数据
 */
PHP_METHOD(workerman_server, recv) {
	int ret;
	zend_long fd;
	zend_long length = 65536; //代表的是字符串的长度 , 不包括字符串结束符号\0

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_LONG(fd)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(length)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	zend_string *buf = zend_string_alloc(length, 0); //申请地址空间。这个申请的长度是length+1 预留了\0的位置
	ret = wmSocket_recv(fd, ZSTR_VAL(buf), length, 0);
	if (ret == 0) {
		zend_update_property_long(workerman_server_ce_ptr, getThis(),
				ZEND_STRL("errCode"), WM_ERROR_SESSION_CLOSED_BY_CLIENT);

		zend_update_property_string(workerman_server_ce_ptr,
				getThis(), ZEND_STRL("errMsg"),
				wm_strerror(WM_ERROR_SESSION_CLOSED_BY_CLIENT));

		//释放掉申请的内存
		zend_string_efree(buf);
		RETURN_FALSE
	}
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "recv error");
		RETURN_FALSE
	}
	ZSTR_VAL(buf)[ret] = '\0'; //适当的位置加上结束符,但是php还是会打出来全部
	RETURN_STR(buf);
}

//发送数据
PHP_METHOD(workerman_server, send) {
	ssize_t retval;
	zend_long fd;
	char *data;
	size_t length;

	ZEND_PARSE_PARAMETERS_START(2, 2)
				Z_PARAM_LONG(fd)
				Z_PARAM_STRING(data, length)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	retval = wmSocket_send(fd, data, length, 0);
	if (retval < 0) {
		php_error_docref(NULL, E_WARNING, "send error");
		RETURN_FALSE
	}
	RETURN_LONG(retval);
}

static const zend_function_entry workerman_server_methods[] = { //
						PHP_ME(workerman_server, __construct, arginfo_workerman_server_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR) //
						PHP_ME(workerman_server, accept, arginfo_workerman_server_void, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_server, recv, arginfo_workerman_server_recv, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_server, send, arginfo_workerman_server_send, ZEND_ACC_PUBLIC) //
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

	//类进行初始化的时候设置变量
	zend_declare_property_long(workerman_server_ce_ptr, ZEND_STRL("sock"), -1,
	ZEND_ACC_PUBLIC);
	zend_declare_property_string(workerman_server_ce_ptr, ZEND_STRL("host"), "",
	ZEND_ACC_PUBLIC);
	zend_declare_property_long(workerman_server_ce_ptr, ZEND_STRL("port"), -1,
	ZEND_ACC_PUBLIC);

	//注册变量和初始值
	zend_declare_property_long(workerman_server_ce_ptr, ZEND_STRL("errCode"), 0,
			ZEND_ACC_PUBLIC);
	zend_declare_property_string(workerman_server_ce_ptr, ZEND_STRL("errMsg"),
			"", ZEND_ACC_PUBLIC);

}
