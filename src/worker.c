#include "worker.h"
#include "coroutine.h"
#include "loop.h"

static unsigned int last_id = 0;
static swHashMap *_workers = NULL; //worker map
static swHashMap *_pidMap = NULL; //worker map
static swHashMap *_fd_workers = NULL; //worker map

static zval* _wm_zval_maxSendBufferSize = NULL;
static zval* _wm_zval_maxPackageSize = NULL;

//解析地址
void parseSocketAddress(wmWorker* worker, zend_string *listen);
void bind_callback(zval* _This, const char* fun_name,
		php_fci_fcc **handle_fci_fcc);
void resumeAccept(wmWorker *worker);

//初始化一下参数
void wm_worker_init() {
	if (!_workers) {
		_workers = swHashMap_new(NULL);
	}
	if (!_pidMap) {
		_pidMap = swHashMap_new(NULL);
	}
	if (!_fd_workers) {
		_fd_workers = swHashMap_new(NULL);
	}
	if (!_wm_zval_maxSendBufferSize) {
		_wm_zval_maxSendBufferSize = emalloc(sizeof(zval));
		ZVAL_STR(_wm_zval_maxSendBufferSize,
				zend_string_init( ZEND_STRL("maxSendBufferSize"), 0));
		_wm_zval_maxSendBufferSize->value.str->gc.refcount++;
	}
	if (!_wm_zval_maxPackageSize) {
		_wm_zval_maxPackageSize = emalloc(sizeof(zval));
		ZVAL_STR(_wm_zval_maxPackageSize,
				zend_string_init( ZEND_STRL("maxPackageSize"), 0));
		_wm_zval_maxPackageSize->value.str->gc.refcount++;
	}
}

/**
 * 创建
 */
wmWorker* wmWorker_create(zval *_This, zend_string *listen) {
	wm_worker_init();

	wmWorker* worker = (wmWorker *) wm_malloc(sizeof(wmWorker));
	bzero(worker, sizeof(wmWorker));
	worker->_status = WM_STATUS_STARTING;
	worker->onWorkerStart = NULL;
	worker->onWorkerReload = NULL;
	worker->onConnect = NULL;
	worker->onMessage = NULL;
	worker->onClose = NULL;
	worker->onBufferFull = NULL;
	worker->onBufferDrain = NULL;
	worker->onError = NULL;
	worker->id = ++last_id; //worker id
	worker->fd = 0;
	worker->backlog = WM_DEFAULT_BACKLOG;
	worker->host = NULL;
	worker->port = 0;
	worker->count = 0;
	worker->transport = NULL;
	worker->_This = _This;

	parseSocketAddress(worker, listen);

	//写入workers对照表
	swHashMap_add_int(_workers, worker->id, worker);
	swHashMap_add_int(_pidMap, worker->id, swHashMap_new(NULL));

	return worker;
}

//启动服务器
bool wmWorker_run(wmWorker *worker) {
	//zval zsocket;
	worker->_status = WM_STATUS_RUNNING;

	worker->fd = wmSocket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	wmSocket_set_nonblock(worker->fd);

	if (wmSocket_bind(worker->fd, worker->host, worker->port) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}

	if (wmSocket_listen(worker->fd, worker->backlog) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}

	swHashMap_add_int(_fd_workers, worker->fd, worker);

	//绑定fd
	zend_update_property_long(workerman_worker_ce_ptr, worker->_This,
			ZEND_STRL("fd"), worker->fd);

	//处理onWorkerStart
	bind_callback(worker->_This, "onWorkerStart", &worker->onWorkerStart);
	if (worker->onWorkerStart) {
		worker->onWorkerStart->fci.param_count = 1;
		worker->onWorkerStart->fci.params = worker->_This;
		if (call_closure_func(worker->onWorkerStart) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
			return false;
		}
		php_printf("12345 \n");
	}
	//在这里应该注册事件回调
	resumeAccept(worker);
	//进入loop
	wmWorkerLoop_loop();
	return true;
}

//关闭服务器
bool wmWorker_stop(wmWorker* worker) {
	worker->_status = WM_STATUS_SHUTDOWN;
	return true;
}

void wmWorker_free(wmWorker* worker) {
	if (worker) {
		swHashMap_free(swHashMap_find_int(_pidMap, worker->id));
		swHashMap_del_int(_pidMap, worker->id);
		swHashMap_del_int(_workers, worker->id);
		swHashMap_del_int(_fd_workers, worker->fd);

		if (worker->onWorkerStart) {
			//减少引用计数
			wm_zend_fci_cache_discard(&worker->onWorkerStart->fcc);
			efree(worker->onWorkerStart);
		}
		if (worker->onConnect) {
			//减少引用计数
			wm_zend_fci_cache_discard(&worker->onConnect->fcc);
			efree(worker->onConnect);
		}
		wm_free(worker);
	}
}

//检查环境
void wmWorker_checkSapiEnv() {
	zend_string* _php_sapi = zend_string_init("PHP_SAPI", strlen("PHP_SAPI"),
			0);
	zval* _php_sapi_zval = zend_get_constant(_php_sapi);
	php_var_dump(_php_sapi_zval, 1 TSRMLS_CC);
	zend_string_free(_php_sapi);
}

//绑定回调
void bind_callback(zval* _This, const char* fun_name,
		php_fci_fcc **handle_fci_fcc) {
	//判断是否有workerStart
	zval *_zval = wm_zend_read_property(workerman_worker_ce_ptr, _This,
			fun_name, strlen(fun_name), 0);
	*handle_fci_fcc = (php_fci_fcc *) ecalloc(1, sizeof(php_fci_fcc));

	char *_error = NULL;
	if (!_zval
			|| zend_parse_arg_func(_zval, &(*handle_fci_fcc)->fci,
					&(*handle_fci_fcc)->fcc, 0, &_error) == 0) {
		efree(*handle_fci_fcc);
		php_error_docref(NULL, E_ERROR, "%s error : %s", fun_name, _error);
	}

	//为这个闭包增加引用计数
	wm_zend_fci_cache_persist(&(*handle_fci_fcc)->fcc);
}

/**
 * 向loop注册accept
 */
void resumeAccept(wmWorker *worker) {
	wmWorkerLoop_add(worker->fd, WM_EVENT_READ);
}

//执行完一个onConnect之后回调
void onConnect_callback(void* _connection) {
	wmConnection* conn = (wmConnection*) _connection;

	//设置属性 start
	if (zend_std_has_property(conn->_This, _wm_zval_maxSendBufferSize,
	ZEND_PROPERTY_ISSET, NULL)) {
		zval* __zval = wm_zend_read_property(workerman_connection_ce_ptr,
				conn->_This, ZEND_STRL("maxSendBufferSize"), 0);
		conn->maxSendBufferSize = __zval->value.lval;
	} else {
		zend_update_property_long(workerman_connection_ce_ptr, conn->_This,
				ZEND_STRL("maxSendBufferSize"), conn->maxSendBufferSize);
	}

	if (zend_std_has_property(conn->_This, _wm_zval_maxPackageSize,
	ZEND_PROPERTY_ISSET, NULL)) {
		zval* __zval = wm_zend_read_property(workerman_connection_ce_ptr,
				conn->_This, ZEND_STRL("maxPackageSize"), 0);
		conn->maxPackageSize = __zval->value.lval;
	} else {
		zend_update_property_long(workerman_connection_ce_ptr, conn->_This,
				ZEND_STRL("maxPackageSize"), conn->maxPackageSize);
	}
	//设置属性 end

}

/**
 * accept回调函数
 */
void _wmWorker_acceptConnection(wmWorker *worker) {
	//新的Connection对象
	zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
	zval *z = emalloc(sizeof(zval));
	ZVAL_OBJ(z, obj);

	wmConnectionObject* connection_object =
			(wmConnectionObject *) wm_connection_fetch_object(obj);

	//接客
	connection_object->connection = wmConnection_accept(worker->fd);
	connection_object->connection->worker = (void*) worker;
	connection_object->connection->_This = z;

	//设置属性 start
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"),
			connection_object->connection->id);
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"),
			connection_object->connection->fd);

	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"),
			connection_object->connection->fd);

	zval* __zval = zend_read_static_property(workerman_connection_ce_ptr,
			ZEND_STRL("defaultMaxSendBufferSize"), 0);

	connection_object->connection->maxSendBufferSize = __zval->value.lval;

	__zval = zend_read_static_property(workerman_connection_ce_ptr,
			ZEND_STRL("defaultMaxPackageSize"), 0);
	connection_object->connection->maxPackageSize = __zval->value.lval;

	zend_update_property_long(workerman_connection_ce_ptr, z,
			ZEND_STRL("maxSendBufferSize"), 4567);

	//设置属性 end

	//设置回调方法 start
	bind_callback(worker->_This, "onConnect", &worker->onConnect);

	bind_callback(worker->_This, "onMessage", &worker->onMessage);
	connection_object->connection->onMessage = worker->onMessage;

	bind_callback(worker->_This, "onClose", &worker->onClose);
	connection_object->connection->onClose = worker->onClose;
	//设置回调方法 end

	//onConnect
	if (worker->onConnect) {
		long _cid = wmCoroutine_create(&(worker->onConnect->fcc), 1, z); //创建新协程
		wmCoroutine_set_callback(_cid, onConnect_callback,
				connection_object->connection);
	}
}

wmWorker* wmWorker_find_by_fd(int fd) {
	return swHashMap_find_int(_fd_workers, fd);
}

/**
 * 解析地址
 */
void parseSocketAddress(wmWorker* worker, zend_string *listen) {
	char *str = strtok(listen->val, ":");
	for (int i = 0; i < 3; i++) {
		if (i == 0 && str != NULL) { //判断协议
			worker->transport = (char*) wm_malloc(sizeof(char) * (strlen(str)));
			strcpy(worker->transport, str);
		}
		if (i == 1 && str != NULL) { //判断host
			int str_i = 0;
			while (str[str_i] == '/') {
				str_i++;
			}
			if ((strlen(str) - str_i) > 0) {
				worker->host = (char*) wm_malloc(
						sizeof(char) * (strlen(str) - str_i));
				strcpy(worker->host, (char *) (str + str_i));
			}
		}
		if (i == 2 && str != NULL) { //判断端口
			worker->port = atoi(str);
		}
		str = strtok(NULL, ":");
	}
}
