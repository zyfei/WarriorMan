#include "worker.h"
#include "coroutine.h"
#include "loop.h"

static unsigned int _last_id = 0;
static wmString *_processTitle = NULL;
static swHashMap *_workers = NULL; //worker map
static swHashMap *_pidMap = NULL; //worker map

/**
 * Mapping from PID to worker process ID.
 * The format is like this [worker_id=>[0=>$pid, 1=>$pid, ..], ..].
 * @var array
 */
static swHashMap *_idMap = NULL;

static swHashMap *_fd_workers = NULL; //worker map
static wmString* _startFile = NULL; //启动文件
static wmString* _pidFile = NULL; //pid路径
static wmString* _runDir = NULL; //启动路径
static int _status = WM_WORKER_STATUS_STARTING; //当前服务状态
static bool _daemonize = false; //守护进程模式

//解析地址
void parseSocketAddress(wmWorker* worker, zend_string *listen);
void bind_callback(zval* _This, const char* fun_name, php_fci_fcc **handle_fci_fcc);
void resumeAccept(wmWorker *worker);
void checkEnv();
void parseCommand();
void init_idMap();

//初始化一下参数
void wmWorker_init() {
	//初始化进程头
	_processTitle = wmString_dup("WorkerMan", 9);

	if (!_workers) {
		_workers = swHashMap_new(NULL);
	}
	if (!_pidMap) {
		_pidMap = swHashMap_new(NULL);
	}
	if (!_fd_workers) {
		_fd_workers = swHashMap_new(NULL);
	}
	if (!_idMap) {
		_idMap = swHashMap_new(NULL);
	}
}

/**
 * 创建
 */
wmWorker* wmWorker_create(zval *_This, zend_string *listen) {
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
	worker->id = ++_last_id; //worker id
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
	swHashMap_add_int(_idMap, worker->id, NULL);

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
	zend_update_property_long(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("fd"), worker->fd);

	//处理onWorkerStart
	bind_callback(worker->_This, "onWorkerStart", &worker->onWorkerStart);
	if (worker->onWorkerStart) {
		worker->onWorkerStart->fci.param_count = 1;
		worker->onWorkerStart->fci.params = worker->_This;
		if (call_closure_func(worker->onWorkerStart) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
			return false;
		}
	}

	//设置回调方法 start
	bind_callback(worker->_This, "onConnect", &worker->onConnect);
	bind_callback(worker->_This, "onMessage", &worker->onMessage);
	bind_callback(worker->_This, "onClose", &worker->onClose);
	bind_callback(worker->_This, "onBufferFull", &worker->onBufferFull);
	bind_callback(worker->_This, "onBufferDrain", &worker->onBufferDrain);
	bind_callback(worker->_This, "onError", &worker->onError);
	//设置回调方法 end

	//在这里应该注册事件回调
	resumeAccept(worker);
	//进入loop
	wmWorkerLoop_loop();
	return true;
}

//全部运行
void wmWorker_runAll() {
	checkEnv();
	parseCommand();
}

//关闭服务器
bool wmWorker_stop(wmWorker* worker) {
	worker->_status = WM_STATUS_SHUTDOWN;
	return true;
}

/**
 * 其实也没有必要去释放
 */
void wmWorker_free(wmWorker* worker) {
	if (worker) {
		swHashMap* hmap = (swHashMap*) swHashMap_find_int(_pidMap, worker->id);
		if (hmap) {
			swHashMap_free(hmap);
			hmap = NULL;
		}

		swHashMap_del_int(_idMap, worker->id);
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
		worker = NULL;
	}
}

//检查环境
void checkEnv() {
	//检查是否是cli模式
	zend_string* _php_sapi = zend_string_init("PHP_SAPI", strlen("PHP_SAPI"), 0);
	zval* _php_sapi_zval = zend_get_constant(_php_sapi);
	if (strcmp(_php_sapi_zval->value.str->val, "cli") != 0) {
		wmError("Only run in command line mode \n");
		return;
	}
	zend_string_free(_php_sapi);

	//获取启动文件
	const char* executed_filename = zend_get_executed_filename();
	_startFile = wmString_dup(executed_filename, sizeof(char) * strlen(executed_filename));

	if (strcmp("[no active file]", executed_filename) == 0) {
		wmError("[no active file]");
		return;
	}
	char* executed_filename_i = strrchr(executed_filename, '/');
	int run_dir_len = executed_filename_i - executed_filename + 1;

	//设置启动路径
	_runDir = wmString_dup(executed_filename, sizeof(char) * (run_dir_len));

	//检查,并且设置pid文件位置
	zval* _pidFile_zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("pidFile"), 0);
	if (!_pidFile_zval) {
		_pidFile = wmString_dup2(_runDir);
		wmString_append_ptr(_pidFile, ZEND_STRL("worker.pid"));

		zend_update_static_property_stringl(workerman_worker_ce_ptr, ZEND_STRL("pidFile"), _pidFile->str, _pidFile->length);
	} else {
		_pidFile = wmString_dup(_pidFile_zval->value.str->val, _pidFile_zval->value.str->len);
	}
	//

	// State.
	_status = WM_WORKER_STATUS_STARTING;

	//检查守护进程模式
	zval* _daemonize_zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("daemonize"), 0);
	if (_daemonize_zval) {
		if (Z_TYPE_INFO_P(_daemonize_zval) == IS_TRUE) {
			_daemonize = true;
		}
	}
	//

	// Process title.
	char process_title[512];
	snprintf(process_title, 512, "%.*s: master process start_file=%.*s", (int) _processTitle->length, _processTitle->str, (int) _startFile->length,
		_startFile->str);
	if (!set_process_title(process_title)) {
		wmError("call_user_function 'cli_set_process_title' error");
		return;
	}

	//为fork进程做准备
	init_idMap();
}

//初始化idMap
void init_idMap() {
	swHashMap_rewind(_workers);
	uint64_t key;
	//循环_workers
	while (1) {
		wmWorker* worker = (wmWorker *) swHashMap_each_int(_workers, &key);
		if (worker == NULL) {
			break;
		}
		//创建一个新map
		worker->count = worker->count < 1 ? 1 : worker->count;
		int* new_id_map = wm_malloc(sizeof(int) * worker->count);
		for (int i = 0; i < worker->count; i++) {
			new_id_map[i] = 0;
		}
		swHashMap_add_int(_idMap, worker->id, new_id_map);
	}
}

//解析用户输入
void parseCommand() {
	//从全局变量中查用户输入argv
	zend_string* argv_str = zend_string_init(ZEND_STRL("argv"), 0);
	zval *argv = zend_hash_find(&EG(symbol_table), argv_str);
	zend_string_free(argv_str);
	HashTable *argv_table = Z_ARRVAL_P(argv);

	zend_string* start_file = NULL; //启动文件
	zend_string* command = NULL; //第一个命令
	zend_string* command2 = NULL; //第二个命令

	//获取启动文件
	zval *value = zend_hash_index_find(argv_table, 0);
	start_file = Z_STR_P(value);

	value = zend_hash_index_find(argv_table, 1);
	if (value != NULL) {
		command = Z_STR_P(value);
	}

	value = zend_hash_index_find(argv_table, 2);
	if (value != NULL) {
		command2 = Z_STR_P(value);
	}
	int command_type = 0;
	command_type = strcmp("start", command->val) == 0 ? 1 : command_type;
	command_type = strcmp("stop", command->val) == 0 ? 2 : command_type;
	command_type = strcmp("restart", command->val) == 0 ? 3 : command_type;
	command_type = strcmp("reload", command->val) == 0 ? 4 : command_type;
	command_type = strcmp("status", command->val) == 0 ? 5 : command_type;
	command_type = strcmp("connections", command->val) == 0 ? 6 : command_type;

	if (command_type == 0) {
		if (command != NULL) {
			php_printf("Unknown command: %s\n", command->val);
		}
		php_printf(
			"Usage: php yourfile <command> [mode]\nCommands: \nstart\t\tStart worker in DEBUG mode.\n\t\tUse mode -d to start in DAEMON mode.\nstop\t\tStop worker.\n\t\tUse mode -g to stop gracefully.\nrestart\t\tRestart workers.\n\t\tUse mode -d to start in DAEMON mode.\n\t\tUse mode -g to stop gracefully.\nreload\t\tReload codes.\n\t\tUse mode -g to reload gracefully.\nstatus\t\tGet worker status.\n\t\tUse mode -d to show live status.\nconnections\tGet worker connections.\n");
		exit(0);
		return;
	}

	//设置守护进程模式
	if (command2 && strcmp("-d", command2->val) == 0) {
		_daemonize = true;
		zend_update_static_property_bool(workerman_worker_ce_ptr, ZEND_STRL("daemonize"), IS_TRUE);
	}

	// execute command.
	switch (command_type) {
	case 1: //如果是start，就继续运行
		break;
	default:
		php_printf("Unknown command: %s\n", command->val);
		exit(0);
		return;
	}
}

//绑定回调
void bind_callback(zval* _This, const char* fun_name, php_fci_fcc **handle_fci_fcc) {
//判断是否有workerStart
	zval *_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, _This, fun_name, strlen(fun_name), 0);
//如果没有
	if (_zval == NULL) {
		return;
	}
	*handle_fci_fcc = (php_fci_fcc *) ecalloc(1, sizeof(php_fci_fcc));
	char *_error = NULL;
	if (!_zval || zend_parse_arg_func(_zval, &(*handle_fci_fcc)->fci, &(*handle_fci_fcc)->fcc, 0, &_error) == 0) {
		efree(*handle_fci_fcc);
		*handle_fci_fcc = NULL;
		php_error_docref(NULL, E_ERROR, "%s error : %s", fun_name, _error);
		return;
	}
//为这个闭包增加引用计数
	wm_zend_fci_cache_persist(&(*handle_fci_fcc)->fcc);
}

/**
 * 向loop注册accept
 */
void resumeAccept(wmWorker *worker) {
//读 | 避免惊群
	wmWorkerLoop_add(worker->fd, WM_EVENT_READ | WM_EVENT_EPOLLEXCLUSIVE);
}

/**
 * accept回调函数
 */
void _wmWorker_acceptConnection(wmWorker *worker) {
//新的Connection对象
	zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
	zval *z = emalloc(sizeof(zval));
	ZVAL_OBJ(z, obj);

	wmConnectionObject* connection_object = (wmConnectionObject *) wm_connection_fetch_object(obj);

//接客
	connection_object->connection = wmConnection_accept(worker->fd);
	connection_object->connection->worker = (void*) worker;
	connection_object->connection->_This = z;

//设置属性 start
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"), connection_object->connection->id);
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);

	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);

//
	zval* __zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxSendBufferSize"), 0);
	connection_object->connection->maxSendBufferSize = __zval->value.lval;
	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxSendBufferSize"), connection_object->connection->maxSendBufferSize);

//
	__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxPackageSize"), 0);
	connection_object->connection->maxPackageSize = __zval->value.lval;

	zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxPackageSize"), connection_object->connection->maxPackageSize);

	zval_ptr_dtor(__zval);
//设置属性 end

//设置回调方法 start
	connection_object->connection->onMessage = worker->onMessage;
	connection_object->connection->onClose = worker->onClose;
	connection_object->connection->onBufferFull = worker->onBufferFull;
	connection_object->connection->onBufferDrain = worker->onBufferDrain;
	connection_object->connection->onError = worker->onError;
//设置回调方法 end

//onConnect
	if (worker->onConnect) {
		wmCoroutine_create(&(worker->onConnect->fcc), 1, z); //创建新协程
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
				worker->host = (char*) wm_malloc(sizeof(char) * (strlen(str) - str_i));
				strcpy(worker->host, (char *) (str + str_i));
			}
		}
		if (i == 2 && str != NULL) { //判断端口
			worker->port = atoi(str);
		}
		str = strtok(NULL, ":");
	}
}

//释放相关资源
void wmWorker_shutdown() {
	if (!_workers) {
		swHashMap_free(_workers);
	}
	if (!_pidMap) {
		swHashMap_free(_pidMap);
	}
	if (!_fd_workers) {
		swHashMap_free(_fd_workers);
	}
	if (!_idMap) {
		swHashMap_free(_idMap);
	}
}
