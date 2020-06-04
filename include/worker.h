#ifndef _WM_SERVER_H
#define _WM_SERVER_H

/**
 * worker的头文件咯
 */
#include "base.h"
#include "connection.h"
#include "coroutine.h"

extern zend_class_entry workerman_worker_ce;
extern zend_class_entry *workerman_worker_ce_ptr;

typedef struct _wmWorker {
	uint32_t id; //worker id
	uint32_t fd;
	zval* _This; //指向当前类的指针
	php_fci_fcc *onWorkerStart;
	php_fci_fcc *onWorkerReload;
	php_fci_fcc *onConnect;
	php_fci_fcc *onMessage;
	php_fci_fcc *onClose;
	php_fci_fcc *onBufferFull;
	php_fci_fcc *onBufferDrain;
	php_fci_fcc *onError;

	int _status; //当前状态
	int32_t backlog; //listen队列长度
	char* host;
	int32_t port;
	int32_t count; //进程数量
	char* transport;
} wmWorker;

//为了通过php对象，找到上面的c++对象 start
typedef struct {
	wmWorker *worker; //c对象 这个是create产生的
	zend_object std; //php对象
} wmWorkerObject;

wmWorkerObject* wm_worker_fetch_object(zend_object *obj);
//为了通过php对象，找到上面的c++对象 end

void wmWorker_init();
void wmWorker_shutdown();

wmWorker* wmWorker_create(zval *_This, zend_string *listen);
bool wmWorker_run(wmWorker *worker_obj); //启动服务器
void wmWorker_runAll();
bool wmWorker_stop(wmWorker* worker); //关闭服务器
void wmWorker_free(wmWorker* worker);
wmWorker* wmWorker_find_by_fd(int fd);
//loop 回调
void _wmWorker_acceptConnection(wmWorker *worker);

#endif
