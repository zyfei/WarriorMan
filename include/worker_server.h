#ifndef _WM_SERVER_H
#define _WM_SERVER_H

/**
 * server的头文件咯
 */
#include "bash.h"
#include "coroutine.h"
#include "coroutine_socket.h"

typedef struct {
	wmCoroutionSocket *socket;
	php_fci_fcc *handler; //接收到客户端连接之后，会回调的函数。
	bool running; //服务器是否正在运行中
} wmServer;

wmServer* wmServer_create(char *host, int port);

bool wmServer_run(wmServer* server); //启动服务器

bool wmServer_stop(wmServer* server); //关闭服务器

void wmServer_set_handler(wmServer* server, php_fci_fcc *_handler);

php_fci_fcc* wmServer_get_handler(wmServer* server);

void wmServer_free(wmServer* server);

#endif
