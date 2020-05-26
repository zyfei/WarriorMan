#include "worker_server.h"

#include "coroutine.h"
#include "coroutine_socket.h"

static unsigned int last_id = 0;
static swHashMap *_workers = NULL; //worker map
static swHashMap *_pidMap = NULL; //worker map

/**
 * 创建
 */
wmServer* wmServer_create() {
	if (!_workers) {
		_workers = swHashMap_new(NULL);
	}
	if (!_pidMap) {
		_pidMap = swHashMap_new(NULL);
	}

	wmServer* server = (wmServer *) wm_malloc(sizeof(wmServer));
	bzero(server, sizeof(wmServer));
	server->socket = NULL;
	server->_status = WM_STATUS_STARTING;
	server->handler = NULL;
	server->onWorkerStart = NULL;
	server->id = ++last_id; //server id
	server->backlog = WM_DEFAULT_BACKLOG;
	server->host = NULL;
	server->port = 0;
	server->count = 0;

	//写入workers对照表
	swHashMap_add_int(_workers, server->id, server);
	swHashMap_add_int(_pidMap, server->id, swHashMap_new(NULL));

	return server;
}

/**
 * 创建
 */
wmServer* wmServer_create2(char *host, int port) {
	int backlog = WM_DEFAULT_BACKLOG;

	if (!_workers) {
		_workers = swHashMap_new(NULL);
	}
	if (!_pidMap) {
		_pidMap = swHashMap_new(NULL);
	}

	wmCoroutionSocket *socket = wmCoroutionSocket_init(AF_INET, SOCK_STREAM, 0);
	if (wmCoroutionSocket_bind(socket, host, port) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return NULL;
	}

	if (wmCoroutionSocket_listen(socket, backlog) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return NULL;
	}

	wmServer* server = (wmServer *) wm_malloc(sizeof(wmServer));
	bzero(server, sizeof(wmServer));
	server->socket = socket;
	server->_status = WM_STATUS_STARTING;
	server->handler = NULL;
	server->onWorkerStart = NULL;
	server->id = ++last_id; //server id
	server->backlog = WM_DEFAULT_BACKLOG;
	server->host = NULL;
	server->port = 0;
	server->count = 0;

	//写入workers对照表
	swHashMap_add_int(_workers, server->id, server);
	swHashMap_add_int(_pidMap, server->id, swHashMap_new(NULL));
	return server;
}

//启动服务器
bool wmServer_run(wmServer* server) {
	zval zsocket;
	server->_status = WM_STATUS_RUNNING;

	//调用onworkerStart 这个不是一个协程
	if (server->onWorkerStart) {
		if (call_closure_func(server->onWorkerStart) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
			return false;
		}
	}

	while (server->_status == WM_STATUS_RUNNING) {
		wmCoroutionSocket* conn = wmCoroutionSocket_accept(server->socket);
		if (!conn) {
			return false;
		}

		php_wm_init_socket_object(&zsocket, conn);

		zend_update_property_long(workerman_socket_ce_ptr, &zsocket,
				ZEND_STRL("fd"), conn->sockfd);

		wmCoroutine_create(&(server->handler->fcc), 1, &zsocket);
		//清空
		zval_dtor(&zsocket);
	}

	//减少引用计数
	wm_zend_fci_cache_discard(&server->handler->fcc);

	return true;
}

//关闭服务器
bool wmServer_stop(wmServer* server) {
	server->_status = WM_STATUS_SHUTDOWN;
	return true;
}

void wmServer_set_handler(wmServer* server, php_fci_fcc *_handler) {
	server->handler = _handler;
}

php_fci_fcc* wmServer_get_handler(wmServer* server) {
	return server->handler;
}

void wmServer_free(wmServer* server) {
	if (server) {
		if (server->handler) {
			efree(server->handler);
			wmServer_set_handler(server, NULL);
		}
		if (server->socket) {
			wmCoroutionSocket_free(server->socket);
		}
		wm_free(server);
	}
}

