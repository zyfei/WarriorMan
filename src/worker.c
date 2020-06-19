#include "worker.h"
#include "coroutine.h"
#include "loop.h"

static unsigned int _last_id = 0;
static wmString *_processTitle = NULL;

static wmHash_INT_PTR *_workers = NULL; //保存所有的worker
static wmWorker* _main_worker = NULL; //当前子进程所属的worker

/**
 * 记录着每个worker，fork的所有pid
 */
static wmHash_INT_PTR* _worker_pids = NULL;
static wmArray* _pid_array_tmp = NULL; //临时存放pid数组的

/**
 * key是fd,value是对应的worker
 */
static wmHash_INT_PTR *_fd_workers = NULL;

static wmString* _startFile = NULL; //启动文件
static wmString* _pidFile = NULL; //pid路径
static wmString* _runDir = NULL; //启动路径
static int _status = WM_WORKER_STATUS_STARTING; //当前服务状态
static bool _daemonize = false; //守护进程模式
static int _masterPid = 0; //master进程ID
static wmString* _logFile = NULL; //记录启动停止等信息
static wmString* _stdoutFile = NULL; //当守护模式运行的时候，所有输出会重定向到这里
static FILE* _stdout = NULL; //重设之后的标准输出
static FILE* _stderr = NULL; //重设之后的标准错误输出

static int _maxUserNameLength = 0; //用户名字的最大长度
static int _maxWorkerNameLength = 0; //名字的最大长度
static int _maxSocketNameLength = 0; //listen的最大长度

static void acceptConnection(wmWorker* worker);
static void parseSocketAddress(wmWorker* worker, zend_string *listen); //解析地址
static void bind_callback(zval* _This, const char* fun_name, php_fci_fcc **handle_fci_fcc);
static void checkEnv();
static void parseCommand();
static void initWorkerPids();
static void daemonize();
static void saveMasterPid();
static void initWorkers();
static void _listen(wmWorker *worker);
static void forkWorkers();
static void forkOneWorker(wmWorker* worker, int key);
static int getKey_by_pid(wmWorker* worker, int pid);
static void _unlisten(wmWorker *worker);
static void installSignal(); //装载信号
static void reinstallSignal(); //针对子进程，使用epoll重新装载信号
static void getAllWorkerPids(); //获取所有子进程pid
static void stopAll(); //停止服务
static void signalHandler(int signal);
static void monitorWorkers();
static void alarm_wait();
static void displayUI();
static void error_callback(int fd, int coro_id);
static void echoWin(const char *format, ...);
static char* getCurrentUser();
void _log(const char *format, ...);
void setUserAndGroup(wmWorker * worker);
void resetStd(); //重设默认输出到文件

//初始化一下参数
void wmWorker_init() {
	//初始化进程标题
	_processTitle = wmString_dup("WarriorMan", 10);
	_workers = wmHash_init(WM_HASH_INT_STR);
	_fd_workers = wmHash_init(WM_HASH_INT_STR);
	_worker_pids = wmHash_init(WM_HASH_INT_STR);
	_pid_array_tmp = wmArray_new(64, sizeof(int));

	wmWorkerLoop_set_handler(WM_EVENT_READ, WM_LOOP_WORKER, WM_LOOP_RESUME);
	wmWorkerLoop_set_handler(WM_EVENT_ERROR, WM_LOOP_WORKER, error_callback);
}

/**
 * 创建
 */
wmWorker* wmWorker_create(zval *_This, zend_string *socketName) {
	wmWorker* worker = (wmWorker *) wm_malloc(sizeof(wmWorker));
	bzero(worker, sizeof(wmWorker));
	worker->_status = WM_WORKER_STATUS_STARTING;
	worker->onWorkerStart = NULL;
	worker->onWorkerStop = NULL;
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
	worker->transport = 0;
	worker->_This = _This;
	worker->name = NULL;
	worker->socketName = wmString_dup(socketName->val, socketName->len);
	worker->stopping = false;
	worker->user = NULL;
	parseSocketAddress(worker, socketName);

	//写入workers对照表
	if (WM_HASH_ADD(WM_HASH_INT_STR, _workers, worker->id,worker) < 0) {
		wmError("wmWorker_create -> _workers_add error!");
	}
	return worker;
}

//服务器监听启动
void _listen(wmWorker *worker) {
	if (worker->fd == 0) {
		worker->fd = wm_socket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		wm_socket_set_nonblock(worker->fd);
		if (wm_socket_bind(worker->fd, worker->host, worker->port) < 0) {
			wmWarn("Error has occurred: port=%d (errno %d) %s", worker->port, errno, strerror(errno));
			return;
		}

		if (wm_socket_listen(worker->fd, worker->backlog) < 0) {
			wmWarn("Error has occurred: fd=%d (errno %d) %s", worker->fd, errno, strerror(errno));
			return;
		}

		if (WM_HASH_ADD(WM_HASH_INT_STR, _fd_workers, worker->fd,worker) < 0) {
			wmWarn("_listen -> _fd_workers_add fail : fd=%d", worker->fd);
			return;
		}

		//绑定fd
		zend_update_property_long(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("fd"), worker->fd);

	}

}

//取消监听
void _unlisten(wmWorker *worker) {
	if (worker->fd != 0) {
		wm_socket_close(worker->fd);
		worker->fd = 0;
	}
}

//启动服务器
void wmWorker_run(wmWorker *worker) {
	//zval zsocket;
	worker->_status = WM_WORKER_STATUS_RUNNING;

	//先处理onWorkerStart,用户将会在这里初始化pdo等，所以这里不是协程
	bind_callback(worker->_This, "onWorkerStart", &worker->onWorkerStart);
	if (worker->onWorkerStart) {
		worker->onWorkerStart->fci.param_count = 1;
		worker->onWorkerStart->fci.params = worker->_This;
		if (call_closure_func(worker->onWorkerStart) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
			return;
		}
	}

	//重新注册信号检测
	reinstallSignal();

	//设置回调方法 start
	bind_callback(worker->_This, "onWorkerStop", &worker->onWorkerStop);
	bind_callback(worker->_This, "onConnect", &worker->onConnect);
	bind_callback(worker->_This, "onMessage", &worker->onMessage);
	bind_callback(worker->_This, "onClose", &worker->onClose);
	bind_callback(worker->_This, "onBufferFull", &worker->onBufferFull);
	bind_callback(worker->_This, "onBufferDrain", &worker->onBufferDrain);
	bind_callback(worker->_This, "onError", &worker->onError);
	//设置回调方法 end

	//注册loop事件
	wmWorkerLoop_add(worker->fd, WM_EVENT_READ | WM_EVENT_EPOLLEXCLUSIVE, WM_LOOP_WORKER);
	//死循环accept，遇到消息就新创建协程处理
	while (1) {
		wmCoroutine_yield();
		acceptConnection(worker);
	}
}

//全部运行
void wmWorker_runAll() {
	checkEnv();
	//设置定时器
	signal(SIGALRM, alarm_wait);
	alarm(1);

	initWorkerPids(); //根据count初始化pid数组
	parseCommand(); //解析用户命令
	daemonize(); //守护进程模式
	initWorkers(); //初始化进程相关资料
	installSignal(); //装载信号
	saveMasterPid();
	displayUI();
	forkWorkers();
	resetStd();
	monitorWorkers(); //监听子进程
}

/**
 * 查看子进程信息，并且处理master定时器
 */
void monitorWorkers() {
	_status = WM_WORKER_STATUS_RUNNING;
	while (1) {
		int zero = 0;
		int status = 0;
		int pid;
		do {
			pid = waitpid(-1, &status, WUNTRACED); //子进程已经退出并且其状态未报告时返回。
		} while (pid < 0 && errno == EINTR);

		if (pid > 0) {
			for (int k = wmHash_begin(_worker_pids); k != wmHash_end(_worker_pids); k++) {
				if (!wmHash_exist(_worker_pids, k)) {
					continue;
				}
				wmArray* pid_arr = wmHash_value(_worker_pids, k);
				int worker_id = wmHash_key(_workers, k);
				for (int i = 0; i < pid_arr->offset; i++) {
					int *_pid = wmArray_find(pid_arr, i);
					if (*_pid == pid) { //找到正主了，就是这个孙子自己先退出了，办他
						wmArray_set(pid_arr, i, &zero);
						wmWorker* worker = WM_HASH_GET(WM_HASH_INT_STR, _workers, worker_id);
						if (WIFEXITED(status) != 0) {
							if (WIFSIGNALED(status)) {
								_log("1worker:%s pid:%d exit with status %d", worker->name->str, pid, WTERMSIG(status));
							} else if (WIFSTOPPED(status)) {
								_log("2worker:%s pid:%d exit with status %d", worker->name->str, pid, WSTOPSIG(status));
							} else {
								_log("3worker:%s pid:%dexit with status unknow", worker->name->str, pid);
							}
						}
						break;
					}
				}
				//
				if (_status != WM_WORKER_STATUS_SHUTDOWN) {
					forkWorkers();
				}
			}

		}
		//等待子进程全部退出
		if (_status == WM_WORKER_STATUS_SHUTDOWN) {
			getAllWorkerPids();
			if (_pid_array_tmp->offset == 0) {
				_log("WarriorMan[%s] has been stopped", _startFile->str);
				exit(0); //直接退出，不给php反应的机会，否则会弹出警告
			}
		}
	}
}

//fork子进程
void forkWorkers() {
	for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
		if (!wmHash_exist(_workers, k)) {
			continue;
		}
		wmWorker* worker = wmHash_value(_workers, k);
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
		wmArray* pid_arr = WM_HASH_GET(WM_HASH_INT_STR, _worker_pids, worker->id);
		wmArray_set(pid_arr, key, &pid);
		return;
	} else if (pid == 0) { // For child processes.
		for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
			if (!wmHash_exist(_workers, k)) {
				continue;
			}
			wmWorker* worker2 = wmHash_value(_workers, k);
			if (worker2->id != worker->id) {
				wmWorker_free(worker2);
			}
		}
		//设置当前子进程运行的是哪个worker
		_main_worker = worker;

		//重设标准输出
		if (_status == WM_WORKER_STATUS_STARTING) {
			resetStd();
		}

		//取消闹钟
		alarm(0);
		//清空定时器,为了不遗传给下一代
		wmTimerWheel_clear(&WorkerG.timer);

		// Process title.
		wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%.*s: worker process %.*s %.*s", (int) _processTitle->length, _processTitle->str,
			(int) worker->name->length, worker->name->str, (int) worker->socketName->length, worker->socketName->str);
		if (!set_process_title(WorkerG.buffer_stack->str)) {
			wmError("call_user_function 'cli_set_process_title' error");
			return;
		}
		setUserAndGroup(worker);
		//创建一个新协程去处理事件

		//创建run协程 start
		zend_fcall_info_cache run;
		wm_get_internal_function(worker->_This, workerman_worker_ce_ptr, ZEND_STRL("run"), &run);
		wmCoroutine_create(&run, 0, NULL);
		//创建run协程 end

		//进入loop
		wmWorkerLoop_loop();
		//_run(worker);
		wmWarn("event-loop exited");
		_log("event-loop exited");
		exit(0);
	} else {
		wmError("forkOneWorker fail");
	}
}

//获取所有的pids
void getAllWorkerPids() {
	wmArray_clear(_pid_array_tmp);

	for (int k = wmHash_begin(_worker_pids); k != wmHash_end(_worker_pids); k++) {
		if (!wmHash_exist(_worker_pids, k)) {
			continue;
		}
		wmArray* pid_arr = wmHash_value(_worker_pids, k);
		for (int i = 0; i < pid_arr->offset; i++) {
			int *pid = wmArray_find(pid_arr, i);
			if (*pid > 0) {
				wmArray_add(_pid_array_tmp, pid);
			}
		}
	}

}

/**
 * 在idmap中，通过value查询key
 */
int getKey_by_pid(wmWorker* worker, int pid) {
	wmArray* pid_arr = WM_HASH_GET(WM_HASH_INT_STR, _worker_pids, worker->id);
	int id = -1;
	for (int i = 0; i < pid_arr->offset; i++) {
		int* _pid = (int*) wmArray_find(pid_arr, i);
		if (pid == *_pid) {
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
	for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
		if (!wmHash_exist(_workers, k)) {
			continue;
		}
		wmWorker* worker = wmHash_value(_workers, k);
		//检查worker->name
		zval *_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("name"), 0);
		if (_zval) {
			if (Z_TYPE_INFO_P(_zval) == IS_STRING) {
				worker->name = wmString_dup(_zval->value.str->val, _zval->value.str->len);
			}
		}
		if (!worker->name) {
			worker->name = wmString_dup("none", 4);
			zend_update_property_stringl(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("name"), worker->name->str, 4);
		}

		_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("user"), 0);
		if (_zval) {
			if (Z_TYPE_INFO_P(_zval) == IS_STRING) {
				worker->user = wm_malloc(sizeof(char) *_zval->value.str->len + 1);
				memcpy(worker->user, _zval->value.str->val, _zval->value.str->len);
				worker->user[_zval->value.str->len] = '\0';
			}
		} else {
			worker->user = getCurrentUser();
			zend_update_property_stringl(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("user"), worker->user, strlen(worker->user));
		}

		//检查当前用户
		if (getuid() != 0 && strncmp(worker->user, getCurrentUser(), strlen(worker->user)) != 0) {
			_log("You must have the root privileges to change uid and gid.");
		}

		//设置用户名字的最大长度
		if (_maxUserNameLength < strlen(worker->user)) {
			_maxUserNameLength = strlen(worker->user);
		}

		//设置worker->name的最大长度
		if (_maxWorkerNameLength < worker->name->length) {
			_maxWorkerNameLength = worker->name->length;
		}

		//设置worker->name的最大长度
		if (_maxSocketNameLength < worker->socketName->length) {
			_maxSocketNameLength = worker->socketName->length;
		}

		//listen
		_listen(worker);
	}
}

/**
 * 直接杀死进程，我是冷酷的杀手
 */
void _kill(void* _pid) {
	int* pid = (int*) _pid;
	kill(*pid, SIGKILL);
}

//worker简易调度器/定时器，由alarm实现
void alarm_wait() {
	long mic_time;
	//检查定时器
	if (WorkerG.timer.num > 0) {
		//获取毫秒
		wmGetMilliTime(&mic_time);
		wmTimerWheel_update(&WorkerG.timer, mic_time);
	}
	alarm(1);
}

//停止全部
void stopAll() {
	_status = WM_WORKER_STATUS_SHUTDOWN;
	if (_masterPid == getpid()) { //主进程
		_log("WarriorMan[%s] stopping ...", _startFile->str);
		getAllWorkerPids(); //获取所有子进程
		for (int i = 0; i < _pid_array_tmp->offset; i++) {
			int* pid = wmArray_find(_pid_array_tmp, i);
			kill(*pid, SIGINT);
			//设置一下两秒后强制杀死
			wmTimerWheel_add_quick(&WorkerG.timer, _kill, (void*) pid, WM_KILL_WORKER_TIMER_TIME);

			//创建定时器，检查状态

		}
	} else { //如果是子进程收到了去世信号
		//循环workers
		if (!_main_worker->stopping) {
			wmWorker_stop(_main_worker);
			_main_worker->stopping = true;
		}
		//停止loop，也就是结束了
		wmWorkerLoop_stop();
		//子进程在这里退出
		exit(0);
	}
}

//关闭服务器
bool wmWorker_stop(wmWorker* worker) {
	worker->_status = WM_WORKER_STATUS_SHUTDOWN;
	if (worker->onWorkerStop) {
		worker->onWorkerStop->fci.param_count = 1;
		worker->onWorkerStop->fci.params = worker->_This;
		if (call_closure_func(worker->onWorkerStop) != SUCCESS) {
			php_error_docref(NULL, E_ERROR, "call onWorkerStop error");
			return false;
		}
	}
	_unlisten(worker);
	if (_stdout != NULL) {
		fclose(_stdout);
	}
	if (_stderr != NULL) {
		fclose(_stderr);
	}
	//关闭所有连接
	wmConnection_close_connections();
	return true;
}

//检查环境
void checkEnv() {
	//检查是不是在协程环境
	wmCoroutine * cor = wmCoroutine_get_current();
	if (cor == NULL) {
		wmError("Only run in coroutine mode \n");
		return;
	}

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
	zval* _zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("pidFile"), 0);
	if (!_zval) {
		_pidFile = wmString_dup2(_runDir);
		wmString_append_ptr(_pidFile, ZEND_STRL("worker.pid"));

		zend_update_static_property_stringl(workerman_worker_ce_ptr, ZEND_STRL("pidFile"), _pidFile->str, _pidFile->length);
	} else {
		_pidFile = wmString_dup(_zval->value.str->val, _zval->value.str->len);
	}
	//

	// State.
	_status = WM_WORKER_STATUS_STARTING;

	//检查守护进程模式
	_zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("daemonize"), 0);
	if (_zval) {
		if (Z_TYPE_INFO_P(_zval) == IS_TRUE) {
			_daemonize = true;
		}
	}

	// Process title.
	wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%.*s: master process start_file=%.*s", (int) _processTitle->length, _processTitle->str,
		(int) _startFile->length, _startFile->str);
	if (!set_process_title(WorkerG.buffer_stack->str)) {
		wmError("call_user_function 'cli_set_process_title' error");
		return;
	}

	//设置Logfile文件位置
	_zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("logFile"), 0);
	if (!_zval) {
		_logFile = wmString_dup2(_runDir);
		wmString_append_ptr(_logFile, ZEND_STRL("warriorman.log"));

		zend_update_static_property_stringl(workerman_worker_ce_ptr, ZEND_STRL("logFile"), _logFile->str, _logFile->length);
	} else {
		_logFile = wmString_dup(_zval->value.str->val, _zval->value.str->len);
	}

	if (access(_logFile->str, F_OK) == 0) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		utimes(_logFile->str, &tv);
		chmod(_logFile->str, 0622);
	}

	//设置stdoutFile文件位置
	_zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("stdoutFile"), 0);
	if (!_zval) {
		_stdoutFile = wmString_dup2(_runDir);
		wmString_append_ptr(_stdoutFile, ZEND_STRL("stdout.log"));

		zend_update_static_property_stringl(workerman_worker_ce_ptr, ZEND_STRL("stdoutFile"), _stdoutFile->str, _stdoutFile->length);
	} else {
		_stdoutFile = wmString_dup(_zval->value.str->val, _zval->value.str->len);
	}

	if (access(_stdoutFile->str, F_OK) == 0) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		utimes(_stdoutFile->str, &tv);
		chmod(_stdoutFile->str, 0622);
	}
}

//初始化idMap
void initWorkerPids() {
	for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
		if (!wmHash_exist(_workers, k)) {
			continue;
		}
		wmWorker* worker = wmHash_value(_workers, k);
		//创建一个新map
		worker->count = worker->count < 1 ? 1 : worker->count;
		wmArray* new_ids = wmArray_new(64, sizeof(int));
		int value = 0;
		for (int i = 0; i < worker->count; i++) {
			wmArray_add(new_ids, &value);
		}
		if (WM_HASH_ADD(WM_HASH_INT_STR, _worker_pids, worker->id,new_ids) < 0) {
			wmError("initWorkerPids -> _worker_pids_add  fail");
		}
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
	zval *value = zend_hash_index_find(argv_table, 0);
	zend_string* start_file = Z_STR_P(value);

	value = zend_hash_index_find(argv_table, 1);
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
			printf("Unknown command: %s\n", command->val);
		}
		printf(
			"Usage: php yourfile <command> [mode]\nCommands: \nstart\t\tStart worker in DEBUG mode.\n\t\tUse mode -d to start in DAEMON mode.\nstop\t\tStop worker.\n\t\tUse mode -g to stop gracefully.\nrestart\t\tRestart workers.\n\t\tUse mode -d to start in DAEMON mode.\n\t\tUse mode -g to stop gracefully.\nreload\t\tReload codes.\n\t\tUse mode -g to reload gracefully.\nstatus\t\tGet worker status.\n\t\tUse mode -d to show live status.\nconnections\tGet worker connections.\n");
		exit(0);
		return;
	}

	//设置守护进程模式
	if (command2 && strcmp("-d", command2->val) == 0) {
		_daemonize = true;
		zend_update_static_property_bool(workerman_worker_ce_ptr, ZEND_STRL("daemonize"), 1);
	}

	// Get master process PID.
	if (access(_pidFile->str, F_OK) == 0) {
		wmString* _masterPidFile = wm_file_get_contents(_pidFile->str);
		_masterPid = atoi(_masterPidFile->str);
		wmString_free(_masterPidFile);
		if (_masterPid <= 0) {
			_masterPid = 0;
		}
	}
	if (_daemonize) {
		_log("WarriorMan[%s] %s in DAEMON mode", start_file->val, command->val);
	} else {
		_log("WarriorMan[%s] %s in DEBUG mode", start_file->val, command->val);
	}

	int master_is_alive = 0;
	if (_masterPid && (kill(_masterPid, 0) != -1) && (getpid() != _masterPid)) {
		master_is_alive = 1;
	}
	if (master_is_alive) {
		if (command_type == 1) {
			_log("WarriorMan[%s] already running ...", start_file->val);
			exit(0);
		}
	} else if (command_type != 1 && command_type != 3) {
		_log("WarriorMan[%s] not run", start_file->val);
		exit(0);
	}
	int i;
	int command_ret = 0;
	// execute command.
	switch (command_type) {
	case 1: //如果是start，就继续运行
		break;
	case 2: //stop
		_log("WarriorMan[%s] is stopping ...", start_file->val);
		kill(_masterPid, SIGINT); //给主进程发送信号
		for (i = 0; i < 5; i++) {
			if (kill(_masterPid, 0) == -1) { //如果死亡了，那么跳出
				command_ret = 1;
				break;
			}
			sleep(1);
		}
		if (command_ret == 1) {
			_log("WarriorMan[%s] stop success", start_file->val);
		} else {
			_log("WarriorMan[%s] stop fail", start_file->val);
		}
		exit(0);
		return;
	default:
		_log("Unknown command: %s", command->val);
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
 * 由run方法循环调用
 */
void acceptConnection(wmWorker* worker) {
	int connfd;
	wmConnection* conn;
	zval* __zval;
	zend_fcall_info_cache call_read;
	for (int i = 0; i < WM_ACCEPT_MAX_COUNT; i++) {

		connfd = wm_socket_accept(worker->fd);
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
		switch (worker->transport) {
		case WM_SOCK_TCP:
			conn = wmConnection_create(connfd, worker->transport);
			break;
		default:
			wmError("unknow transport")
			;
		}
		if (conn == NULL) {
			wmWarn("_wmWorker_acceptConnection() -> wmConnection_create failed")
			return;
		}

		//新的Connection对象
		zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
		zval *z = emalloc(sizeof(zval));
		ZVAL_OBJ(z, obj);

		wmConnectionObject* connection_object = (wmConnectionObject *) wm_connection_fetch_object(obj);

		//接客
		connection_object->connection = conn;
		connection_object->connection->worker = (void*) worker;
		connection_object->connection->_This = z;

		//设置属性 start
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"), connection_object->connection->id);
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);
		__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxSendBufferSize"), 0);

		connection_object->connection->maxSendBufferSize = __zval->value.lval;
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxSendBufferSize"), connection_object->connection->maxSendBufferSize);

		__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxPackageSize"), 0);
		connection_object->connection->maxPackageSize = __zval->value.lval;
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxPackageSize"), connection_object->connection->maxPackageSize);

		zval_ptr_dtor(__zval);
		//设置属性 end

		//设置socket属性start
		conn->socket->maxSendBufferSize = conn->maxSendBufferSize;
		conn->socket->maxPackageSize = conn->maxPackageSize;
		//设置socket属性end

		//设置回调方法 start
		conn->onMessage = worker->onMessage;
		conn->onClose = worker->onClose;
		conn->onBufferFull = worker->onBufferFull;
		conn->onBufferDrain = worker->onBufferDrain;
		conn->onError = worker->onError;
		//设置回调方法 end

		//onConnect
		if (worker->onConnect) {
			wmCoroutine_create(&(worker->onConnect->fcc), 1, z); //创建新协程
		}

		//创建协程 conn开始读 start
		wm_get_internal_function(conn->_This, workerman_connection_ce_ptr, ZEND_STRL("read"), &call_read);
		wmCoroutine_create(&call_read, 0, NULL);
		//创建协程 conn开始读  end
	}
}

/**
 * 解析地址
 */
void parseSocketAddress(wmWorker* worker, zend_string *listen) {
	char *transport = strstr(listen->val, "://");
	int transport_len = transport - listen->val;
	if (transport == NULL) {
		wmError("parseSocketAddress error listen=%s", listen->val); //协议解析失败
	}
	if (strncmp("tcp", listen->val, transport_len) == 0) {
		worker->transport = WM_SOCK_TCP;
	}
	if (strncmp("dup", listen->val, transport_len) == 0) {
		worker->transport = WM_SOCK_UDP;
	}
	if (worker->transport == 0) {
		wmError("parseSocketAddress error listen=%s , only the tcp,udp", listen->val); //协议解析失败
	}
	int len = transport_len + 3;
	char *s1 = listen->val + len;
	int s1_len = listen->len - len;
	if (!isdigit(listen->val[len])) {
		wmError("parseSocketAddress error listen=%s , please like 'tcp://0.0.0.0:1234'", listen->val); //协议解析失败
	}
	zend_string* err = NULL;
	worker->host = parse_ip_address_ex(s1, s1_len, &worker->port, 1, &err);
	if (err) {
		wmError("%s", err->val);
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
	wm_file_put_contents(_pidFile->str, WorkerG.buffer_stack->str, ret, false); //写入PID文件
}

/**
 * 信号处理函数
 */
void signalHandler(int signal) {
	switch (signal) {
	// Stop.
	case SIGINT:
		stopAll();
		break;
		// Reload.
	case SIGUSR1:
		printf("Reload haven't started to do\n");
		//static::$_pidsToRestart = static::getAllWorkerPids();
		//_reload();
		break;
		// Show status.
	case SIGUSR2:
		printf("Status haven't started to do\n");
		//static::writeStatisticsToStatusFile();
		break;
		// Show connection status.
	}
}

/**
 * 装载信号
 */
void installSignal() {
	// stop
	signal(SIGINT, signalHandler);
	// reload
	signal(SIGUSR1, signalHandler);
	// status
	signal(SIGUSR2, signalHandler);
	// ignore
	signal(SIGPIPE, SIG_IGN); //忽略由于对端连接关闭，导致进程退出的问题
}

void reinstallSignal() {
	// 忽略 stop
	signal(SIGINT, SIG_IGN);
	// 忽略 reload
	signal(SIGUSR1, SIG_IGN);
	// 忽略 status
	signal(SIGUSR2, SIG_IGN);

	wmWorkerLoop_add_sigal(SIGINT, signalHandler);
	wmWorkerLoop_add_sigal(SIGUSR1, signalHandler);
	wmWorkerLoop_add_sigal(SIGUSR2, signalHandler);
}

char* getCurrentUser() {
	struct passwd *pwd;
	pwd = getpwuid(getuid());
	return pwd->pw_name;
}

void displayUI() {
	// show version
	echoWin("<n>-----------------------<w> %s </w>-----------------------------\n</n>", _processTitle->str);
	echoWin("WarriorMan version:%s          PHP version:%s\n", PHP_WORKERMAN_VERSION, PHP_VERSION);
	echoWin("--------------------------<w> WORKERS </w>-----------------------------\n");
	echoWin("<w>user</w>%-*s<w>worker</w>%-*s<w>listen</w>%-*s<w>processes</w> <w>status</w>\n", //
		_maxUserNameLength + 2 - strlen("user"), "", //
		_maxWorkerNameLength + 2 - strlen("worker"), "", //
		_maxSocketNameLength + 2 - strlen("listen"), "");
	for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
		if (!wmHash_exist(_workers, k)) {
			continue;
		}
		wmWorker* worker = wmHash_value(_workers, k);
		int count_space_num = worker->count > 9 ? 2 : 1;
		count_space_num = worker->count > 99 ? 3 : count_space_num;
		count_space_num = worker->count > 999 ? 4 : count_space_num;

		echoWin("%-*s%-*s%-*s %d%-*s <g> [OK] </g>\n", //
			_maxUserNameLength + 2, worker->user, //
			_maxWorkerNameLength + 2, worker->name->str, //
			_maxSocketNameLength + 2, worker->socketName->str, //
			worker->count, 10 - count_space_num, "");
	}

	echoWin("----------------------------------------------------------------\n");
	if (_daemonize) {
		echoWin("Input \"php %s\" to quit. Start success.\n\n", _startFile->str);
	} else {
		echoWin("Press Ctrl-C to quit. Start success.\n");
	}

}

/**
 * 只向窗口输出
 */
void echoWin(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int retval = vsnprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, format, args);
	va_end(args);
	if (retval < 0) {
		retval = 0;
		WorkerG.buffer_stack->str[0] = '\0';
	} else if (retval >= WorkerG.buffer_stack->size) {
		retval = WorkerG.buffer_stack->size - 1;
		WorkerG.buffer_stack->str[retval] = '\0';
	}
	WorkerG.buffer_stack->length = retval;
	wmString_replace(WorkerG.buffer_stack, "<n>", "\033[1A\n\033[K");
	wmString_replace(WorkerG.buffer_stack, "<w>", "\033[47;30m");
	wmString_replace(WorkerG.buffer_stack, "<g>", "\033[32;40m");
	wmString_replace(WorkerG.buffer_stack, "</n>", "\033[0m");
	wmString_replace(WorkerG.buffer_stack, "</w>", "\033[0m");
	wmString_replace(WorkerG.buffer_stack, "</g>", "\033[0m");

	//标准输出
	fputs(WorkerG.buffer_stack->str, stdout);
	fflush(stdout); //刷新缓冲区
}

void _log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = vsnprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, format, args);
	va_end(args);
	WorkerG.buffer_stack->length = ret;
	wmString_append_ptr(WorkerG.buffer_stack, "\n", 2);

	if (!_daemonize) {
		echoWin("%s", WorkerG.buffer_stack->str);
	}
	char date[128];
	wm_get_date(date, sizeof(date));

	ret = wm_snprintf(WorkerG.buffer_stack_large->str, WorkerG.buffer_stack_large->size, "%s pid:%d %s", date, getpid(), WorkerG.buffer_stack->str);
	wm_file_put_contents(_logFile->str, WorkerG.buffer_stack_large->str, ret, true); //写入PID文件
}

/**
 * 设置运行组和用户
 */
void setUserAndGroup(wmWorker * worker) {
	struct passwd * pw;
	pw = getpwnam(worker->user);
	if (!pw) {
		_log("Warning: User %s not exsits", worker->user);
		return;
	}
	int uid = pw->pw_uid;
	int gid = pw->pw_gid;

	if (uid != getuid() || gid != getgid()) {
		if (!setgid(gid) || !initgroups(worker->user, gid) || !setuid(uid)) {
			_log("Warning: change gid or uid fail.");
		}
	}
}

//重设默认输出
void resetStd() {
	if (!_daemonize) {
		return;
	}
	_stdout = freopen(_stdoutFile->str, "a", stdout);
	_stderr = freopen(_stdoutFile->str, "a", stderr);
}

wmWorker* wmWorker_find_by_fd(int fd) {
	return WM_HASH_GET(WM_HASH_INT_STR, _fd_workers, fd);
}

void wmWorker_free(wmWorker* worker) {
	_unlisten(worker);
	if (worker->socketName != NULL) {
		wmString_free(worker->socketName);
	}
	efree(worker->_This);
	if (worker->host != NULL) {
		wm_free(worker->host);
	}
	if (worker->name != NULL) {
		wmString_free(worker->name);
	}
	if (worker->onWorkerStart != NULL) {
		efree(worker->onWorkerStart);
		wm_zend_fci_cache_discard(&worker->onWorkerStart->fcc);
	}
	if (worker->onWorkerReload != NULL) {
		efree(worker->onWorkerReload);
		wm_zend_fci_cache_discard(&worker->onWorkerReload->fcc);
	}
	if (worker->onConnect != NULL) {
		efree(worker->onConnect);
		wm_zend_fci_cache_discard(&worker->onConnect->fcc);
	}
	if (worker->onMessage != NULL) {
		efree(worker->onMessage);
		wm_zend_fci_cache_discard(&worker->onMessage->fcc);
	}
	if (worker->onClose != NULL) {
		efree(worker->onClose);
		wm_zend_fci_cache_discard(&worker->onClose->fcc);
	}
	if (worker->onBufferFull != NULL) {
		efree(worker->onBufferFull);
		wm_zend_fci_cache_discard(&worker->onBufferFull->fcc);
	}
	if (worker->onBufferDrain != NULL) {
		efree(worker->onBufferDrain);
		wm_zend_fci_cache_discard(&worker->onBufferDrain->fcc);
	}
	if (worker->onError != NULL) {
		efree(worker->onError);
		wm_zend_fci_cache_discard(&worker->onError->fcc);
	}
	//删除并且释放所有连接
	wmConnection_close_connections();

	//从workers中删除
	WM_HASH_DEL(WM_HASH_INT_STR, _workers, worker->id);
	WM_HASH_DEL(WM_HASH_INT_STR, _fd_workers, worker->fd);

	wm_free(worker);
	worker = NULL;
}

/**
 * epoll发生错误，直接关闭
 */
void error_callback(int fd, int coro_id) {
	wmError("Worker_error_callback error,child worker exit(1)");
}

void wmWorker_shutdown() {
	//初始化进程标题
	wmString_free(_processTitle);
	wmHash_destroy(WM_HASH_INT_STR,_workers);
	wmHash_destroy(WM_HASH_INT_STR,_fd_workers);
	wmHash_destroy(WM_HASH_INT_STR,_worker_pids);
	wmArray_free(_pid_array_tmp);
}
