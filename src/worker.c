#include "worker.h"
#include "coroutine.h"
#include "loop.h"

static unsigned int _last_id = 0;
static wmString *_processTitle = NULL;
static swHashMap *_workers = NULL; //workerId=>worker

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
static int _masterPid = 0; //master进程ID

//解析地址
void parseSocketAddress(wmWorker* worker, zend_string *listen);
void bind_callback(zval* _This, const char* fun_name, php_fci_fcc **handle_fci_fcc);
void resumeAccept(wmWorker *worker);
void checkEnv();
void parseCommand();
void init_idMap();
void daemonize();
void saveMasterPid();
void initWorkers();
void _listen(wmWorker *worker);
void _run(wmWorker *worker);
void forkWorkers();
void forkOneWorker(wmWorker* worker, int key);
int getKey_by_pid(wmWorker* worker, int pid);
void _unlisten(wmWorker *worker);

//初始化一下参数
void wmWorker_init() {
	//初始化进程头
	_processTitle = wmString_dup("WorkerMan", 9);

	if (!_workers) {
		_workers = swHashMap_new(NULL);
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
wmWorker* wmWorker_create(zval *_This, zend_string *socketName) {
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
	worker->key = 0;
	worker->transport = NULL;
	worker->_This = _This;
	worker->name = NULL;
	worker->socketName = wmString_dup(socketName->val, socketName->len);
	parseSocketAddress(worker, socketName);

	//写入workers对照表
	swHashMap_add_int(_workers, worker->id, worker);
	swHashMap_add_int(_idMap, worker->id, NULL);

	return worker;
}

//服务器监听启动
void _listen(wmWorker *worker) {
	if (worker->fd == 0) {
		worker->fd = wmSocket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		wmSocket_set_nonblock(worker->fd);
		if (wmSocket_bind(worker->fd, worker->host, worker->port) < 0) {
			wmWarn("Error has occurred: port=%d (errno %d) %s", worker->port, errno, strerror(errno));
			return;
		}

		if (wmSocket_listen(worker->fd, worker->backlog) < 0) {
			wmWarn("Error has occurred: fd=%d (errno %d) %s", worker->fd, errno, strerror(errno));
			return;
		}
		swHashMap_add_int(_fd_workers, worker->fd, worker);

		//绑定fd
		zend_update_property_long(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("fd"), worker->fd);

	}

}

//取消监听
void _unlisten(wmWorker *worker) {
	if (worker->fd != 0) {
		wmSocket_close(worker->fd);
		worker->fd = 0;
	}
}

//启动服务器
void _run(wmWorker *worker) {
	//zval zsocket;
	worker->_status = WM_STATUS_RUNNING;
	//在这里应该注册事件回调
	resumeAccept(worker);

	//处理onWorkerStart
	bind_callback(worker->_This, "onWorkerStart", &worker->onWorkerStart);
	if (worker->onWorkerStart) {
		worker->onWorkerStart->fci.param_count = 1;
		worker->onWorkerStart->fci.params = worker->_This;
		if (call_closure_func(worker->onWorkerStart) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
			return;
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

	//进入loop
	wmWorkerLoop_loop();
}

//全部运行
void wmWorker_runAll() {
	checkEnv();
	parseCommand(); //解析用户命令
	daemonize(); //守护进程模式
	initWorkers(); //初始化进程相关资料
	saveMasterPid();
	forkWorkers();
	sleep(3000000);
}

/**
 * 向loop注册accept
 */
void resumeAccept(wmWorker *worker) {
	//读 | 避免惊群
	wmWorkerLoop_add(worker->fd, WM_EVENT_READ | WM_EVENT_EPOLLEXCLUSIVE);
}

//fork子进程
void forkWorkers() {
	swHashMap_rewind(_workers);
	uint64_t key;
	//循环_workers
	while (1) {
		wmWorker* worker = (wmWorker *) swHashMap_each_int(_workers, &key);
		if (worker == NULL) {
			break;
		}
		while (1) {
			int key = getKey_by_pid(worker, 0);
			if (key < 0) {
				break;
			}
			forkOneWorker(worker, key);
		}
	}
}

/**
 * 正式fork进程
 */
void forkOneWorker(wmWorker* worker, int key) {
	//拿到一个未fork的进程
	int pid = fork();
	if (pid > 0) { // For master process.
		int* id_arr = (int*) swHashMap_find_int(_idMap, worker->id);
		id_arr[key] = pid;
		return;
	} else if (pid == 0) { // For child processes.

		swHashMap_rewind(_workers);
		uint64_t key;
		wmWorker* delete_worker = NULL;
		//循环_workers
		while (1) {
			wmWorker* worker2 = (wmWorker *) swHashMap_each_int(_workers, &key);
			//后删除
			if (delete_worker != NULL) {
				_unlisten(delete_worker);
				swHashMap_del_int(_workers, delete_worker->id);
				swHashMap_del_int(_fd_workers, delete_worker->fd);
				delete_worker = NULL;
			}
			if (worker2 == NULL) {
				break;
			}
			if (worker2->id != worker->id) {
				delete_worker = worker2;
			}
		}


		// Process title.
		wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%.*s: worker process %.*s %.*s", (int) _processTitle->length, _processTitle->str,
			(int) worker->name->length, worker->name->str, (int) worker->socketName->length, worker->socketName->str);
		if (!set_process_title(WorkerG.buffer_stack->str)) {
			wmError("call_user_function 'cli_set_process_title' error");
			return;
		}
		worker->key = key;
		_run(worker);
		wmError("event-loop exited");
		exit(0);
	} else {
		wmError("forkOneWorker fail");
	}
}

/**
 * 在idmap中，通过value查询key
 */
int getKey_by_pid(wmWorker* worker, int pid) {
	int* id_arr = (int *) swHashMap_find_int(_idMap, worker->id);
	int id = -1;
	for (int i = 0; i < worker->count; i++) {
		if (id_arr[i] == pid) {
			id = i;
			break;
		}
	}
	return id;
}

/**
 * 初始化进程相关资料
 */
void initWorkers() {
	swHashMap_rewind(_workers);
	uint64_t key;
	//循环_workers
	while (1) {
		wmWorker* worker = (wmWorker *) swHashMap_each_int(_workers, &key);
		if (worker == NULL) {
			break;
		}
		//检查worker->name
		zval *_name = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("name"), 0);
		if (_name) {
			if (Z_TYPE_INFO_P(_name) == IS_STRING) {
				worker->name = wmString_dup(_name->value.str->val, _name->value.str->len);
			}
		}
		if (!worker->name) {
			worker->name = wmString_dup("none", 4);
			zend_update_property_stringl(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("name"), worker->name->str, 4);
		}
		//listen
		_listen(worker);
	}
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
		efree(worker->_This);
		swHashMap_del_int(_idMap, worker->id);
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
	wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%.*s: master process start_file=%.*s", (int) _processTitle->length, _processTitle->str,
		(int) _startFile->length, _startFile->str);
	if (!set_process_title(WorkerG.buffer_stack->str)) {
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

	//zend_string* start_file = NULL; //启动文件
	zend_string* command = NULL; //第一个命令
	zend_string* command2 = NULL; //第二个命令

	//获取启动文件
	//zval *value = zend_hash_index_find(argv_table, 0);
	//start_file = Z_STR_P(value);

	zval *value = zend_hash_index_find(argv_table, 1);
	if (value != NULL) {
		command = Z_STR_P(value);
	}

	value = zend_hash_index_find(argv_table, 2);
	if (value != NULL) {
		command2 = Z_STR_P(value);
	}
	int command_type = 0;
	if (command != NULL) {
		command_type = (strcmp("start", command->val) == 0 ? 1 : command_type);
		command_type = (strcmp("stop", command->val) == 0 ? 2 : command_type);
		command_type = (strcmp("restart", command->val) == 0 ? 3 : command_type);
		command_type = (strcmp("reload", command->val) == 0 ? 4 : command_type);
		command_type = (strcmp("status", command->val) == 0 ? 5 : command_type);
		command_type = (strcmp("connections", command->val) == 0 ? 6 : command_type);
	}

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
		zend_update_static_property_bool(workerman_worker_ce_ptr, ZEND_STRL("daemonize"), 1);
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
 * accept回调函数
 */
void _wmWorker_acceptConnection(wmWorker *worker) {
	int connfd;
	wmConnection* _conn;
	zval* __zval;
	for (int i = 0; i < WM_ACCEPT_MAX_COUNT; i++) {
		connfd = wmSocket_accept(worker->fd);
		if (connfd < 0) {
			switch (errno) {
			case EAGAIN: //队列空了，没有信息要读了
				return;
			case EINTR: //被信号中断了，但是还有信息要读
				continue;
			default:
				wmWarn("accept() failed")
				return;
			}
		}
		_conn = wmConnection_create(connfd);
		_conn->events = WM_EVENT_READ;
		//添加读监听
		wmWorkerLoop_add(_conn->fd, _conn->events);

		//新的Connection对象
		zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
		zval *z = emalloc(sizeof(zval));
		ZVAL_OBJ(z, obj);

		wmConnectionObject* connection_object = (wmConnectionObject *) wm_connection_fetch_object(obj);

		//接客
		connection_object->connection = _conn;
		connection_object->connection->worker = (void*) worker;
		connection_object->connection->_This = z;

		//设置属性 start
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"), connection_object->connection->id);
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);

		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);

		//
		__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxSendBufferSize"), 0);
		connection_object->connection->maxSendBufferSize = __zval->value.lval;
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxSendBufferSize"), connection_object->connection->maxSendBufferSize);

		//
		__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxPackageSize"), 0);
		connection_object->connection->maxPackageSize = __zval->value.lval;

		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxPackageSize"), connection_object->connection->maxPackageSize);

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
		zval_ptr_dtor(__zval);
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

/**
 * 开启守护进程模式
 */
void daemonize() {
	if (_daemonize == false) {
		return;
	}
	umask(0);
	int pid = fork();
	if (pid > 0) {
		exit(0); //是父进程，结束父进程
		return;
	} else if (pid < 0) { //fork失败
		wmError("Fork fail");
		return;
	}
	if (setsid() < 0) {
		wmError("Setsid fail");
		return;
	}

	// Fork again avoid SVR4 system regain the control of terminal.
	pid = fork();
	if (pid > 0) {
		exit(0); //是父进程，结束父进程
	} else if (pid < 0) { //fork失败
		wmError("Fork fail");
	}
}

/**
 * 保存master进程ID
 */
void saveMasterPid() {
	_masterPid = getpid();
	int ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%d", _masterPid);
	wm_file_put_contents(_pidFile->str, WorkerG.buffer_stack->str, ret); //写入PID文件
}

//释放相关资源
void wmWorker_shutdown() {
	if (!_workers) {
		swHashMap_free(_workers);
	}
	if (!_fd_workers) {
		swHashMap_free(_fd_workers);
	}
	if (!_idMap) {
		swHashMap_free(_idMap);
	}
}
