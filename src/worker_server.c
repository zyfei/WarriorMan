#include "worker_server.h"

#include "coroutine.h"
#include "coroutine_socket.h"

/**
 * 创建
 */
wmServer* wmServer_create(char *host, int port) {
	wmCoroutionSocket *socket = wmCoroutionSocket_init(AF_INET, SOCK_STREAM,
			0);
	if (wmCoroutionSocket_bind(socket, host, port) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return NULL;
	}

	if (wmCoroutionSocket_listen(socket, 512) < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return NULL;
	}

	wmServer* server = (wmServer *) wm_malloc(sizeof(wmServer));
	bzero(server, sizeof(wmServer));
	server->socket = socket;
	server->running = false;
	server->handler = NULL;
	return server;
}

//启动服务器
bool wmServer_run(wmServer* server) {
	zval zsocket;
	server->running = true;

	while (server->running) {
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
	server->running = false;
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

