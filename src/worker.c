#include "worker.h"
#include "coroutine.h"

static unsigned int last_id = 0;
static swHashMap *_workers = NULL; //worker map
static swHashMap *_pidMap = NULL; //worker map

//解析地址
void parseSocketAddress(wmWorker* worker, zend_string *listen);
void bind_onWorkerStart(wmWorker *worker);
void bind_onConnect(wmWorker *worker, wmConnection *connection);
void acceptConnection(void *_worker);
void resumeAccept(wmWorker *worker);

/**
 * 创建
 */
wmWorker* wmWorker_create(zval *_This, zend_string *listen) {
	//zend_string
	if (!_workers) {
		_workers = swHashMap_new(NULL);
	}
	if (!_pidMap) {
		_pidMap = swHashMap_new(NULL);
	}

	wmWorker* worker = (wmWorker *) wm_malloc(sizeof(wmWorker));
	bzero(worker, sizeof(wmWorker));
	worker->connection = NULL;
	worker->_status = WM_STATUS_STARTING;
	worker->handler = NULL;
	worker->onWorkerStart = NULL;
	worker->onWorkerReload = NULL;
	worker->onConnect = NULL;
	worker->onMessage = NULL;
	worker->onClose = NULL;
	worker->onBufferFull = NULL;
	worker->onBufferDrain = NULL;
	worker->onError = NULL;
	worker->id = ++last_id; //worker id
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

	wmConnection *connection = wmConnection_init(AF_INET, SOCK_STREAM, 0);
	if (wmConnection_bind(connection, worker->host, worker->port) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}

	if (wmConnection_listen(connection, worker->backlog) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return false;
	}

	//绑定fd
	zend_update_property_long(workerman_worker_ce_ptr, worker->_This,
			ZEND_STRL("fd"), connection->sockfd);

	connection->_This = NULL;
	worker->connection = connection;

	//处理onWorkerStart
	bind_onWorkerStart(worker);
	if (worker->onWorkerStart) {
		if (call_closure_func(worker->onWorkerStart) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
			return false;
		}
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

void wmWorker_set_handler(wmWorker* worker, php_fci_fcc *_handler) {
	worker->handler = _handler;
}

php_fci_fcc* wmWorker_get_handler(wmWorker* worker) {
	return worker->handler;
}

void wmWorker_free(wmWorker* worker) {
	if (worker) {
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
		if (worker->handler) {
			efree(worker->handler);
			wmWorker_set_handler(worker, NULL);
		}
		if (worker->connection) {
			wmConnection_free(worker->connection);
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

//判断是否有onWorkerStart并绑定
void bind_onWorkerStart(wmWorker *worker) {
	zval* _This = worker->_This;

	//判断是否有workerStart
	zval *_zval = wm_zend_read_property(workerman_worker_ce_ptr, _This,
			ZEND_STRL("onWorkerStart"), 0);
	php_fci_fcc *handle_fci_fcc = (php_fci_fcc *) ecalloc(1,
			sizeof(php_fci_fcc));

	char *_error = NULL;
	if (!_zval
			|| zend_parse_arg_func(_zval, &handle_fci_fcc->fci,
					&handle_fci_fcc->fcc, 0, &_error) == 0) {
		efree(handle_fci_fcc);
		php_error_docref(NULL, E_ERROR, "onWorkerStart error : %s", _error);
	}

	//为这个闭包增加引用计数
	wm_zend_fci_cache_persist(&handle_fci_fcc->fcc);

	worker->onWorkerStart = handle_fci_fcc;
	worker->onWorkerStart->fci.param_count = 1;
	worker->onWorkerStart->fci.params = _This;
}

//判断是否有onWorkerStart并绑定
void bind_onConnect(wmWorker *worker, wmConnection *connection) {
	zval* _This = worker->_This;

	//判断是否有workerStart
	zval *_zval = wm_zend_read_property(workerman_worker_ce_ptr, _This,
			ZEND_STRL("onConnect"), 0);
	php_fci_fcc *handle_fci_fcc = (php_fci_fcc *) ecalloc(1,
			sizeof(php_fci_fcc));

	char *_error = NULL;
	if (!_zval
			|| zend_parse_arg_func(_zval, &handle_fci_fcc->fci,
					&handle_fci_fcc->fcc, 0, &_error) == 0) {
		efree(handle_fci_fcc);
		php_error_docref(NULL, E_ERROR, "onWorkerStart error : %s", _error);
	}

	//为这个闭包增加引用计数
	wm_zend_fci_cache_persist(&handle_fci_fcc->fcc);

	worker->onConnect = handle_fci_fcc;
	worker->onConnect->fci.param_count = 1;
	worker->onConnect->fci.params = connection->_This;
}

/**
 * 向loop注册accept
 */
void resumeAccept(wmWorker *worker) {
	wmWorkerLoop_add(worker->connection->sockfd, WM_EVENT_READ,
			acceptConnection, worker);
}

/**
 * accept回调函数
 */
void acceptConnection(void *_worker) {
	wmWorker *worker = (wmWorker *) _worker;
	//新的Connection对象
	zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
	zval *z = emalloc(sizeof(zval));
	ZVAL_OBJ(z, obj);

	wmConnectionObject* connection_object =
			(wmConnectionObject *) wm_connection_fetch_object(obj);

	//接客
	connection_object->connection = wmConnection_accept(worker->connection);

	connection_object->connection->_This = z;
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"),
			connection_object->connection->id);
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"),
				connection_object->connection->sockfd);

	//检查绑定
	if (!worker->onConnect) {
		bind_onConnect(worker, connection_object->connection);
	}

	//onConnect
	if (worker->onConnect) {
		wmCoroutine_create(&(worker->onConnect->fcc),
				worker->onConnect->fci.param_count,
				worker->onConnect->fci.params); //创建新协程
	}
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
