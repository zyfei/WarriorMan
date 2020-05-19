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

wmServer* wm_server_create(char *host, int port);

bool wm_server_run(wmServer* server); //启动服务器

bool wm_server_stop(wmServer* server); //关闭服务器

void wm_server_set_handler(wmServer* server, php_fci_fcc *_handler);

php_fci_fcc* wm_server_get_handler();

php_fci_fcc* wm_server_get_handler(wmServer* server);

void wm_server_free(wmServer* server);

#endif
