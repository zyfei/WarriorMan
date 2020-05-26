#ifndef _WM_SERVER_H
#define _WM_SERVER_H

/**
 * server的头文件咯
 */
#include "bash.h"
#include "coroutine.h"
#include "coroutine_socket.h"

typedef struct _wmServer {
	uint32_t id; //server id
	wmCoroutionSocket *socket;
	php_fci_fcc *onWorkerStart; //onWorkerStart回调
	php_fci_fcc *handler;
	int _status; //当前状态
	int32_t backlog; //listen队列长度
	char* host;
	int32_t port;
	int32_t count; //进程数量
} wmServer;

wmServer* wmServer_create();

wmServer* wmServer_create2(char *host, int port);

bool wmServer_run(wmServer* server); //启动服务器

bool wmServer_stop(wmServer* server); //关闭服务器

void wmServer_set_handler(wmServer* server, php_fci_fcc *_handler);

php_fci_fcc* wmServer_get_handler(wmServer* server);

void wmServer_free(wmServer* server);

#endif
