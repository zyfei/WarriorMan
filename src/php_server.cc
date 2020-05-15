/**
 * server入口文件
 */
#include "bash.h"
#include "coroutine_socket.h"

/**
 * 定义 zend class entry
 */
zend_class_entry workerman_server_ce;
zend_class_entry *workerman_server_ce_ptr;

PHP_METHOD(workerman_server, __construct);
PHP_METHOD(workerman_server, accept);
PHP_METHOD(workerman_server, recv);
PHP_METHOD(workerman_server, send);
PHP_METHOD(workerman_server, close);


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

//连接关闭
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_server_close, 0, 0, 1) ZEND_ARG_INFO(0, fd)
ZEND_END_ARG_INFO()

PHP_METHOD(workerman_server, __construct) {
	zval *zhost;
	zend_long zport;
	zval zsock;

	//声明参数获取
	ZEND_PARSE_PARAMETERS_START(2, 2)
				Z_PARAM_ZVAL(zhost)
				Z_PARAM_LONG(zport)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	wmCoroutionSocket *sock = wm_coroution_socket_init(AF_INET, SOCK_STREAM, 0);
	wm_coroution_socket_bind(sock, WM_SOCK_TCP, Z_STRVAL_P(zhost), zport);
	wm_coroution_socket_listen(sock);

	//把创建出来的sock结构体放进了zsock这个zval容器里面
	ZVAL_PTR(&zsock, sock);

	zend_update_property(workerman_server_ce_ptr, getThis(), ZEND_STRL("zsock"),
			&zsock);
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
	wmCoroutionSocket *sock;
	int connfd;
	//读取socket,最后0是采用默认模式，如果没有只警告
	zsock = wm_zend_read_property(workerman_server_ce_ptr, getThis(),
			ZEND_STRL("zsock"), 0);
	sock = (wmCoroutionSocket *) Z_PTR_P(zsock); // 修改的一行
	//开始接客
	connfd = wm_coroution_socket_accept(sock);
	RETURN_LONG(connfd);
}

/**
 * 接收数据
 */
PHP_METHOD(workerman_server, recv) {
	ssize_t ret;
	zend_long fd;
	zend_long length = WM_BUFFER_SIZE_BIG;

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_LONG(fd)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(length)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	//在这里初始化这根客户端管道
	wmCoroutionSocket *conn = wm_coroution_socket_find_by_fd(fd);
	if (conn == NULL) {
		conn = wm_coroution_socket_init_by_fd(fd);
	}

	wm_coroution_socket_recv(conn, length);
	ret = conn->read_buffer->length;

	//客户端已关闭
	if (ret == 0) {
		zend_update_property_long(workerman_server_ce_ptr, getThis(),
				ZEND_STRL("errCode"), WM_ERROR_SESSION_CLOSED_BY_CLIENT);

		zend_update_property_string(workerman_server_ce_ptr,
		getThis(), ZEND_STRL("errMsg"),
				wm_strerror(WM_ERROR_SESSION_CLOSED_BY_CLIENT));

		wm_coroution_socket_close(conn);
		RETURN_FALSE
	}
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "recv error");
		RETURN_FALSE
	}

	//计数重新开始
	conn->read_buffer->length = 0;
	RETURN_STRINGL(conn->read_buffer->str, ret);
}

//发送数据
PHP_METHOD(workerman_server, send) {
	ssize_t ret;
	zend_long fd;
	char *data;
	size_t length;

	ZEND_PARSE_PARAMETERS_START(2, 2)
				Z_PARAM_LONG(fd)
				Z_PARAM_STRING(data, length)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);
	//在这里初始化这根客户端管道
	wmCoroutionSocket *conn = wm_coroution_socket_find_by_fd(fd);
	if (conn == NULL) {
		conn = wm_coroution_socket_init_by_fd(fd);
	}

	ret = wm_coroution_socket_send(conn, data, length);
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "send error");
		//释放掉申请的内存
		wm_coroution_socket_close(conn);
		RETURN_FALSE
	}
	RETURN_LONG(ret);
}

PHP_METHOD(workerman_server, close) {
	int ret;
	zend_long fd;

	ZEND_PARSE_PARAMETERS_START(1, 1)
				Z_PARAM_LONG(fd)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);
	wmCoroutionSocket *conn = wm_coroution_socket_find_by_fd(fd);
	if (conn != NULL) {
		ret = wm_coroution_socket_close(conn);
	}
	if (ret < 0) {
		php_error_docref(NULL, E_WARNING, "close error");
		RETURN_FALSE
	}
	RETURN_LONG(ret);
}

static const zend_function_entry workerman_server_methods[] = { //
						PHP_ME(workerman_server, __construct, arginfo_workerman_server_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR) //
						PHP_ME(workerman_server, accept, arginfo_workerman_server_void, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_server, recv, arginfo_workerman_server_recv, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_server, send, arginfo_workerman_server_send, ZEND_ACC_PUBLIC) //
						PHP_ME(workerman_server, close, arginfo_workerman_server_close, ZEND_ACC_PUBLIC) //
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
	zval *zsock = (zval *) malloc(sizeof(zval));
	zend_declare_property(workerman_server_ce_ptr, ZEND_STRL("zsock"), zsock,
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
