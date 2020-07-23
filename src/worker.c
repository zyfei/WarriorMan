#include "worker.h"
#include "wm_signal.h"
#include "connection.h"
#include "coroutine.h"
#include "loop.h"

static unsigned int _last_id = 0;
static wmString *_processTitle = NULL;

static wmHash_INT_PTR *_workers = NULL; //保存所有的worker
static wmWorker *_main_worker = NULL; //当前子进程所属的worker

/**
 * 记录着每个worker，fork的所有pid
 */
static wmHash_INT_PTR *_worker_pids = NULL;
static wmArray *_pid_array_tmp = NULL; //临时存放pid数组的

static wmArray *_pidsToReload = NULL; //等待reload的pid

/**
 * key是fd,value是对应的worker
 */
static wmHash_INT_PTR *_fd_workers = NULL;

static wmString *_startFile = NULL; //启动文件
static wmString *_pidFile = NULL; //pid路径
static wmString *_runDir = NULL; //启动路径
static int _status = WM_WORKER_STATUS_STARTING; //当前服务状态
static bool _daemonize = false; //守护进程模式
static int _masterPid = 0; //master进程ID
static wmString *_logFile = NULL; //记录启动停止等信息
static wmString *_stdoutFile = NULL; //当守护模式运行的时候，所有输出会重定向到这里
static FILE *_stdout = NULL; //重设之后的标准输出
static FILE *_stderr = NULL; //重设之后的标准错误输出
static wmString *_statisticsFile = NULL; //服务状态文件地址

static int _maxUserNameLength = 4; //用户名字的最大长度
static int _maxWorkerNameLength = 6; //名字的最大长度
static int _maxSocketNameLength = 6; //listen的最大长度
static long _start_timestamp = 0; //服务启动时间戳
static int reload_coro_num = 2; //reload Worker的时候，框架占用的协程数

static void acceptConnectionTcp(wmWorker *worker);
static void acceptConnectionUdp(wmWorker *worker);
static void parseSocketAddress(wmWorker *worker, zend_string *listen); //解析地址
static void bind_callback(zval *_This, const char *fun_name, php_fci_fcc **handle_fci_fcc);
static void checkEnv();
static void parseCommand();
static void initWorkerPids();
static void daemonize();
static void saveMasterPid();
static void initWorker(wmWorker *worker);
static void initWorkers();
static void forkWorkers();
static void forkOneWorker(wmWorker *worker, int key);
static int getKey_by_pid(wmWorker *worker, int pid);
static void _unlisten(wmWorker *worker);
static void installSignal(); //装载信号
static void reinstallSignal(); //针对子进程，使用epoll重新装载信号
static void getAllWorkerPids(); //获取所有子进程pid
static void signalHandler(int signal);
static void monitorWorkers();
static void alarm_wait();
static void displayUI();
static void echoWin(const char *format, ...);
static char* getCurrentUser();
static void _log(const char *format, ...);
static void setUserAndGroup(wmWorker *worker);
static void resetStd(); //重设默认输出到文件
static void reload(); //平滑重启
static void writeStatisticsToStatusFile(); //写入status信息
static bool worker_stop(wmWorker *worker);

//初始化一下参数
void wmWorker_init() {
	//初始化进程标题
	_processTitle = wmString_dup("WarriorMan", 10);
	_workers = wmHash_init(WM_HASH_INT_STR);
	_fd_workers = wmHash_init(WM_HASH_INT_STR);
	_worker_pids = wmHash_init(WM_HASH_INT_STR);
	_pid_array_tmp = wmArray_new(64, sizeof(int));
	_pidsToReload = wmArray_new(64, sizeof(int));
}

/**
 * 创建
 */
wmWorker* wmWorker_create(zval *_This, zend_string *socketName) {
	wmWorker *worker = (wmWorker*) wm_malloc(sizeof(wmWorker));
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
	worker->workerId = _last_id++; //worker id
	worker->fd = 0;
	worker->backlog = WM_DEFAULT_BACKLOG;
	worker->host = NULL;
	worker->port = 0;
	worker->count = 1; //默认是一个进程
	worker->transport = 0;
	worker->_This = _This;
	worker->name = NULL;
	worker->socketName = wmString_dup(socketName->val, socketName->len);
	worker->stopping = false;
	worker->user = NULL;
	worker->socket = NULL;
	worker->protocol = NULL;
	worker->protocol_ce = NULL;
	worker->reloadable = true;
	worker->reusePort = false;
	parseSocketAddress(worker, socketName);

	//说明是在worker进程内，再创建的worker
	if (_main_worker) {
		reload_coro_num++;
		//手动给这个worker加有个引用计数，省的出来就死
		GC_ADDREF(Z_OBJ_P(_This));
	}

	//写入workers对照表
	if (WM_HASH_ADD(WM_HASH_INT_STR, _workers, worker->workerId,worker) < 0) {
		wmError("wmWorker_create -> _workers_add error!");
	}
	return worker;
}

//服务器监听启动
void wmWorker_listen(wmWorker *worker) {
	//初始化单个worker的资料
	initWorker(worker);
	if (worker->socket == NULL) {
		worker->socket = wmSocket_create(worker->transport, WM_LOOP_SEMI_AUTO);
		if (worker->socket == NULL) {
			wmError("transport error");
		}
		worker->fd = worker->socket->fd;

		//开启端口复用
		if (worker->reusePort && wm_socket_reuse_port(worker->fd) < 0) {
			wmSocket_close(worker->socket);
			wmError("set reusePort error");
		}

		if (wm_socket_bind(worker->fd, worker->host, worker->port) < 0) {
			wmWarn("Error has occurred: port=%d (errno %d) %s", worker->port, errno, strerror(errno));
			return;
		}
		//绑定fd
		zend_update_property_long(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("fd"), worker->fd);

		//udp不需要listen
		if (worker->transport == WM_SOCK_TCP) {
			if (wm_socket_listen(worker->fd, worker->backlog) < 0) {
				wmWarn("Error has occurred: fd=%d (errno %d) %s", worker->fd, errno, strerror(errno));
				return;
			}
		}

		if (WM_HASH_ADD(WM_HASH_INT_STR, _fd_workers, worker->fd,worker) < 0) {
			wmWarn("_listen -> _fd_workers_add fail : fd=%d", worker->fd);
			return;
		}

	}
}

//取消监听
void _unlisten(wmWorker *worker) {
	if (worker->socket) {
		worker->fd = 0;
		wmSocket_free(worker->socket);
		worker->socket = NULL;
	}
}

//启动服务器
void wmWorker_run(wmWorker *worker) {
	//重新注册信号检测
	reinstallSignal();
	wmWorker_resumeAccept(worker);
}

// 开始工作
void wmWorker_resumeAccept(wmWorker *worker) {
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

	//设置回调方法 start
	bind_callback(worker->_This, "onWorkerReload", &worker->onWorkerReload);
	bind_callback(worker->_This, "onWorkerStop", &worker->onWorkerStop);
	bind_callback(worker->_This, "onConnect", &worker->onConnect);
	bind_callback(worker->_This, "onMessage", &worker->onMessage);
	bind_callback(worker->_This, "onClose", &worker->onClose);
	bind_callback(worker->_This, "onBufferFull", &worker->onBufferFull);
	bind_callback(worker->_This, "onBufferDrain", &worker->onBufferDrain);
	bind_callback(worker->_This, "onError", &worker->onError);
	//设置回调方法 end

	switch (worker->transport) {
	case WM_SOCK_TCP:
		acceptConnectionTcp(worker);
		break;
	case WM_SOCK_UDP:
		acceptConnectionUdp(worker);
		break;
	default:
		wmError("unknow transport")
	}

}

//全部运行
void wmWorker_runAll() {
	checkEnv();
	parseCommand(); //解析用户命令
	//设置定时器
	signal(SIGALRM, alarm_wait);
	alarm(1);
	initWorkers(); //初始化进程相关资料
	initWorkerPids(); //根据count初始化pid数组
	daemonize(); //守护进程模式
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
				wmArray *pid_arr = wmHash_value(_worker_pids, k);
				int worker_id = wmHash_key(_worker_pids, k);
				for (int i = 0; i < pid_arr->offset; i++) {
					int *_pid = wmArray_find(pid_arr, i);
					if (*_pid == pid) { //找到正主了，就是这个孙子自己先退出了，办他
						wmArray_set(pid_arr, i, &zero);
						wmWorker *worker = WM_HASH_GET(WM_HASH_INT_STR, _workers, worker_id);
						if (WIFEXITED(status) == 0) { //不是正常结束的
							int exit_code = WEXITSTATUS(status);
							if (WIFSIGNALED(status)) {
								_log("worker:%s pid:%d exit(%d) with status %d (WIFSIGNALED)", worker->name->str, pid, exit_code, WTERMSIG(status));
							} else if (WIFSTOPPED(status)) {
								_log("worker:%s pid:%d exit(%d) with status %d (WIFSTOPPED)", worker->name->str, pid, exit_code, WSTOPSIG(status));
							} else {
								_log("worker:%s pid:%d exit(%d)", worker->name->str, pid, exit_code);
							}
						}
						break;
					}
				}
				//
				if (_status != WM_WORKER_STATUS_SHUTDOWN) {
					forkWorkers();

					for (int i = 0; i < _pidsToReload->offset; i++) {
						int *reload_pid = wmArray_find(_pidsToReload, i);
						if (*reload_pid == pid) {
							wmArray_set(_pidsToReload, i, &zero);
							reload();
							break;
						}
					}
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
		wmWorker *worker = wmHash_value(_workers, k);
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
void forkOneWorker(wmWorker *worker, int key) {
	//拿到一个未fork的进程
	int pid = fork();
	if (pid > 0) { // For master process.
		wmArray *pid_arr = WM_HASH_GET(WM_HASH_INT_STR, _worker_pids, worker->workerId);
		wmArray_set(pid_arr, key, &pid);
		return;
	} else if (pid == 0) { // For child processes.
		for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
			if (!wmHash_exist(_workers, k)) {
				continue;
			}
			wmWorker *worker2 = wmHash_value(_workers, k);

			if (worker2->workerId != worker->workerId) {
				wmWorker_free(worker2);
			}
		}
		worker->id = key;
		zend_update_property_long(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("id"), key);

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
		wmArray *pid_arr = wmHash_value(_worker_pids, k);
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
int getKey_by_pid(wmWorker *worker, int pid) {
	wmArray *pid_arr = WM_HASH_GET(WM_HASH_INT_STR, _worker_pids, worker->workerId);
	int id = -1;
	for (int i = 0; i < pid_arr->offset; i++) {
		int *_pid = (int*) wmArray_find(pid_arr, i);
		if (pid == *_pid) {
			id = i;
			break;
		}
	}
	return id;
}

/**
 * 初始化worker相关资料
 */
void initWorker(wmWorker *worker) {
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

	//检查count
	_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("count"), 0);
	if (_zval) {
		if (Z_TYPE_INFO_P(_zval) != IS_LONG) {
			wmError("count must be a number");
		}
		worker->count = Z_LVAL_P(_zval);
	} else {
		zend_update_property_long(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("count"), worker->count);
	}

	//检查应用级协议
	_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("protocol"), 0);
	if (_zval) {
		if (Z_TYPE_INFO_P(_zval) == IS_STRING) {
			worker->protocol = _zval->value.str;
			worker->protocol_ce = zend_lookup_class(worker->protocol);
			if (!worker->protocol_ce) {
				wmError("worker->protocol error 1");
			}
			if ((worker->protocol_ce->ce_flags & (ZEND_ACC_INTERFACE | ZEND_ACC_TRAIT)) != 0) {
				wmError("worker->protocol error 2");
			}
		}
	}

	//检查端口复用
	_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("reusePort"), 0);
	if (_zval) {
		if (Z_TYPE_INFO_P(_zval) == IS_TRUE) {
			worker->reusePort = true;
		}
		zend_update_property_bool(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("reusePort"), worker->reusePort);
	}

	_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("user"), 0);
	if (_zval) {
		if (Z_TYPE_INFO_P(_zval) == IS_STRING) {
			worker->user = wm_malloc(sizeof(char) * _zval->value.str->len + 1);
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

	//设置connections
	array_init(&worker->connections);
	zend_update_property(workerman_worker_ce_ptr, worker->_This, ZEND_STRL("connections"), &worker->connections);
	HT_FLAGS(Z_ARRVAL_P(&worker->connections)) |= HASH_FLAG_ALLOW_COW_VIOLATION;
}

/**
 * 初始化多个worker相关资料
 */
void initWorkers() {
	for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
		if (!wmHash_exist(_workers, k)) {
			continue;
		}
		wmWorker *worker = wmHash_value(_workers, k);
		//listen
		wmWorker_listen(worker);
	}
}

/**
 * 直接杀死进程，我是冷酷的杀手
 */
void _kill(void *_pid) {
	int *pid = (int*) _pid;
	if (*pid != 0) {
		kill(*pid, SIGKILL);
	}
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
void wmWorker_stopAll() {
	_status = WM_WORKER_STATUS_SHUTDOWN;
	if (_masterPid == getpid()) { //主进程
		_log("WarriorMan[%s] stopping ...", _startFile->str);
		getAllWorkerPids(); //获取所有子进程
		for (int i = 0; i < _pid_array_tmp->offset; i++) {
			int *pid = wmArray_find(_pid_array_tmp, i);
			kill(*pid, SIGINT);
			//设置一下两秒后强制杀死
			wmTimerWheel_add_quick(&WorkerG.timer, _kill, (void*) pid, WM_KILL_WORKER_TIMER_TIME);

			//创建定时器，检查状态

		}
	} else { //如果是子进程收到了去世信号
		//循环workers
		if (!_main_worker->stopping) {
			worker_stop(_main_worker);
			_main_worker->stopping = true;
		}
		//停止loop，也就是结束了
		wmWorkerLoop_stop();
		//子进程在这里退出
		exit(0);
	}
}

//关闭服务器
bool worker_stop(wmWorker *worker) {
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
	wmConnection_closeConnections();
	return true;
}

//检查环境
void checkEnv() {
	//检查是否是cli模式
	zend_string *_php_sapi = zend_string_init("PHP_SAPI", strlen("PHP_SAPI"), 0);
	zval *_php_sapi_zval = zend_get_constant(_php_sapi);
	if (strcmp(_php_sapi_zval->value.str->val, "cli") != 0) {
		wmError("Only run in command line mode \n");
		return;
	}
	zend_string_free(_php_sapi);

	//检查workers的数量
	if (_workers->size == 0) {
		wmError("There is no worker to run \n");
		return;
	}

	//获取启动文件
	const char *executed_filename = zend_get_executed_filename();
	_startFile = wmString_dup(executed_filename, sizeof(char) * strlen(executed_filename));

	if (strcmp("[no active file]", executed_filename) == 0) {
		wmError("[no active file]");
		return;
	}
	char *executed_filename_i = strrchr(executed_filename, '/');
	int run_dir_len = executed_filename_i - executed_filename + 1;

	//设置启动路径
	_runDir = wmString_dup(executed_filename, sizeof(char) * (run_dir_len));

	//检查,并且设置pid文件位置
	zval *_zval = wm_zend_read_static_property_not_null(workerman_worker_ce_ptr, ZEND_STRL("pidFile"), 0);
	if (!_zval) {
		_pidFile = wmString_dup2(_runDir);
		int pid_file_lem = _pidFile->length;
		wmString_append(_pidFile, _startFile);
		wmString_append_ptr(_pidFile, ZEND_STRL(".pid"));
		for (int i = pid_file_lem; i < _pidFile->length; i++) {
			if (_pidFile->str[i] == '/') {
				_pidFile->str[i] = '_';
			}
		}
		zend_update_static_property_stringl(workerman_worker_ce_ptr, ZEND_STRL("pidFile"), _pidFile->str, _pidFile->length);
	} else {
		_pidFile = wmString_dup(_zval->value.str->val, _zval->value.str->len);
	}

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

	//设置status文件位置
	_statisticsFile = wmString_dup("/tmp", 4);
	wmString_append(_statisticsFile, _startFile);
	for (int i = 4; i < _statisticsFile->length; i++) {
		if (_statisticsFile->str[i] == '/') {
			_statisticsFile->str[i] = '_';
		}
	}

	//设置启动时间戳
	wmGetMilliTime(&_start_timestamp);
	_start_timestamp = _start_timestamp / 1000;
}

//初始化idMap
void initWorkerPids() {
	for (int k = wmHash_begin(_workers); k != wmHash_end(_workers); k++) {
		if (!wmHash_exist(_workers, k)) {
			continue;
		}
		wmWorker *worker = wmHash_value(_workers, k);
		//创建一个新map
		worker->count = worker->count < 1 ? 1 : worker->count;
		wmArray *new_ids = wmArray_new(64, sizeof(int));
		int value = 0;
		for (int i = 0; i < worker->count; i++) {
			wmArray_add(new_ids, &value);
		}
		if (WM_HASH_ADD(WM_HASH_INT_STR, _worker_pids, worker->workerId,new_ids) < 0) {
			wmError("initWorkerPids -> _worker_pids_add  fail");
		}
	}

}

//解析用户输入
void parseCommand() {
	//从全局变量中查用户输入argv
	zend_string *argv_str = zend_string_init(ZEND_STRL("argv"), 0);
	zval *argv = zend_hash_find(&EG(symbol_table), argv_str);
	zend_string_free(argv_str);
	HashTable *argv_table = Z_ARRVAL_P(argv);

	//zend_string* start_file = NULL; //启动文件
	zend_string *command = NULL; //第一个命令
	zend_string *command2 = NULL; //第二个命令

	//获取启动文件
	zval *value = zend_hash_index_find(argv_table, 0);
	zend_string *start_file = Z_STR_P(value);

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
		wmString *_masterPidFile = wm_file_get_contents(_pidFile->str);
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
	case 3: //restart
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
			if (command_type == 2) {
				exit(0);
				return;
			}
		} else {
			_log("WarriorMan[%s] stop fail", start_file->val);
			exit(0);
			return;
		}
		return;
	case 4: //reload
		kill(_masterPid, SIGUSR1); //给主进程平滑重启信号
		exit(0);
		return;
	case 5: //status
		while (1) {
			if (access(_statisticsFile->str, F_OK) == 0) {
				remove(_statisticsFile->str); //删除之前的状态文件
			}
			kill(_masterPid, SIGUSR2); //给主进程平滑重启信号
			sleep(1); //睡一秒
			if (_daemonize) {
				fputs("\33[H\33[2J\33(B\33[m", stdout);
				fflush(stdout); //刷新缓冲区
			}
			//读取statusFile信息
			wmString *_status_buffer = wm_file_get_contents(_statisticsFile->str);
			echoWin(_status_buffer->str);
			wmString_free(_status_buffer);
			if (!_daemonize) {
				exit(0);
			}
			echoWin("\nPress Ctrl+C to quit.\n\n");
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
void bind_callback(zval *_This, const char *fun_name, php_fci_fcc **handle_fci_fcc) {
	//判断是否有workerStart
	zval *_zval = wm_zend_read_property_not_null(workerman_worker_ce_ptr, _This, fun_name, strlen(fun_name), 0);
	//如果没有
	if (_zval == NULL) {
		return;
	}
	*handle_fci_fcc = (php_fci_fcc*) ecalloc(1, sizeof(php_fci_fcc));
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
 * 监控tcp连接
 */
void acceptConnectionTcp(wmWorker *worker) {
	//防止惊群
	//wmWorkerLoop_add(worker->socket, WM_EVENT_EPOLLEXCLUSIVE);

	wmConnection *conn;
	zval *__zval;
	zend_fcall_info_cache call_read;
	while (worker->_status == WM_WORKER_STATUS_RUNNING) {
		//wmSocket *socket = wmSocket_accept(worker->socket, WM_LOOP_SEMI_AUTO, WM_SOCKET_MAX_TIMEOUT);
		wmSocket *socket = wmSocket_accept(worker->socket, WM_LOOP_AUTO, WM_SOCKET_MAX_TIMEOUT);
		if (socket == NULL) {
			if (worker->_status != WM_WORKER_STATUS_RUNNING) {
				break;
			}
			wmWarn("acceptConnection fail. workerId=%d , socket = NULL errno=%d", worker->workerId, errno);
			continue;
		}

		conn = wmConnection_create(socket);
		if (conn == NULL) {
			wmWarn("_wmWorker_acceptConnection() -> wmConnection_create failed")
			return;
		}

		//新的Connection对象
		zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
		ZVAL_OBJ(&conn->_This, obj);
		zval *z = &conn->_This;
		wmConnectionObject *connection_object = (wmConnectionObject*) wm_connection_fetch_object(obj);

		//接客
		connection_object->connection = conn;
		connection_object->connection->worker = worker;

		//将connection放入worker->connection中
		add_index_zval(&worker->connections, conn->id, z);
		//引用手动+1，在删除的时候会自动-1
		GC_ADDREF(obj);

		//设置属性 start
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"), connection_object->connection->id);
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);
		__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxSendBufferSize"), 0);

		connection_object->connection->maxSendBufferSize = __zval->value.lval;
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxSendBufferSize"), connection_object->connection->maxSendBufferSize);

		__zval = zend_read_static_property(workerman_connection_ce_ptr, ZEND_STRL("defaultMaxPackageSize"), 0);
		connection_object->connection->maxPackageSize = __zval->value.lval;
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("maxPackageSize"), connection_object->connection->maxPackageSize);

		//设置worker
		zend_update_property(workerman_connection_ce_ptr, z, ZEND_STRL("worker"), worker->_This);

		//设置属性 end

		//设置socket属性start
		conn->socket->maxSendBufferSize = conn->maxSendBufferSize;
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
		wm_get_internal_function(&conn->_This, workerman_connection_ce_ptr, ZEND_STRL("read"), &call_read);
		wmCoroutine_create(&call_read, 0, NULL);
		//创建协程 conn开始读  end
	}
}

/**
 * 由run方法循环调用
 * 监控udp消息
 */
void acceptConnectionUdp(wmWorker *worker) {

	//注册loop事件,半自动
	wmWorkerLoop_add(worker->socket, WM_EVENT_EPOLLEXCLUSIVE);

	//死循环accept，遇到消息就新创建协程处理
	wmConnection *conn;
	while (!worker->socket->closed) {
		conn = wmConnection_create_udp(worker->fd);
		//新的Connection对象
		zend_object *obj = wm_connection_create_object(workerman_connection_ce_ptr);
		zval *z = &conn->_This;
		ZVAL_OBJ(z, obj);

		wmConnectionObject *connection_object = (wmConnectionObject*) wm_connection_fetch_object(obj);

		//接客
		connection_object->connection = conn;
		connection_object->connection->worker = (void*) worker;

		//设置属性 start
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("id"), connection_object->connection->id);
		zend_update_property_long(workerman_connection_ce_ptr, z, ZEND_STRL("fd"), connection_object->connection->fd);
		//设置属性 end
		//设置回调方法 start
		conn->onMessage = worker->onMessage;
		//设置回调方法 end
		wmConnection_recvfrom(conn, worker->socket);
	}
}

/**
 * 解析地址
 */
void parseSocketAddress(wmWorker *worker, zend_string *listen) {
	char *transport = strstr(listen->val, "://");
	int transport_len = transport - listen->val;
	if (transport == NULL) {
		wmError("parseSocketAddress error listen=%s", listen->val); //协议解析失败
	}
	if (strncmp("tcp", listen->val, transport_len) == 0) {
		worker->transport = WM_SOCK_TCP;
	} else if (strncmp("udp", listen->val, transport_len) == 0) {
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
	zend_string *err = NULL;
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
 * 平滑重启
 */
void reload() {
	if (_masterPid == getpid()) { //主进程
		if (_status != WM_WORKER_STATUS_RELOADING && _status != WM_WORKER_STATUS_SHUTDOWN) {
			_log("WarriorMan[%s] reloading", _startFile->str);
			_status = WM_WORKER_STATUS_RELOADING;
		}
		/////////////////////////////////////////////////////////
		//保存需要reload的pid
		wmArray *reloadable_pid_array = wmArray_new(64, sizeof(int));
		for (int k = wmHash_begin(_worker_pids); k != wmHash_end(_worker_pids); k++) {
			if (!wmHash_exist(_worker_pids, k)) {
				continue;
			}
			wmArray *pid_arr = wmHash_value(_worker_pids, k);
			int worker_id = wmHash_key(_worker_pids, k);
			wmWorker *worker = WM_HASH_GET(WM_HASH_INT_STR, _workers, worker_id);

			//循环所有这个worker的子进程
			for (int i = 0; i < pid_arr->offset; i++) {
				int *_pid = wmArray_find(pid_arr, i);
				if (worker->reloadable) {
					wmArray_add(reloadable_pid_array, _pid);
				} else { //如果不可以reload，直接发送信号，子进程会自己判断
					kill(*_pid, SIGUSR1);
				}
			}
		}

		//获取所有需要被reload的pid
		int zero = 0;
		for (int i = 0; i < _pidsToReload->offset; i++) {
			int *_pid = wmArray_find(_pidsToReload, i);
			int _en = 0;
			for (int i2 = 0; i2 < reloadable_pid_array->offset; i2++) {
				int *_pid2 = wmArray_find(reloadable_pid_array, i2);
				if (*_pid == *_pid2) {
					_en = 1;
					break;
				}
			}
			if (_en == 0) {
				wmArray_set(_pidsToReload, i, &zero);
			}
		}

		wmArray_free(reloadable_pid_array);
		/**
		 * 取一个pid reload，这块要配合monitorWorkers，在monitorWorkers中pid死亡如果在_pidToReload中会触发reload方法，继续reload
		 */
		int *reload_pid = NULL;
		for (int i = 0; i < _pidsToReload->offset; i++) {
			int *_pid = wmArray_find(_pidsToReload, i);
			if (*_pid != 0) {
				reload_pid = _pid;
				break;
			}
		}
		if (!reload_pid) {
			if (_status != WM_WORKER_STATUS_SHUTDOWN) {
				_status = WM_WORKER_STATUS_RUNNING;
			}
			return;
		}
		//发送信号
		kill(*reload_pid, SIGUSR1);
		//两秒后强制杀死
		wmTimerWheel_add_quick(&WorkerG.timer, _kill, (void*) reload_pid, WM_KILL_WORKER_TIMER_TIME);
	} else { //子进程,注意这里子进程是运行在信号的协程中，可以使用协程方法
		//调用
		if (_main_worker->onWorkerReload) {
			_main_worker->onWorkerReload->fci.param_count = 1;
			_main_worker->onWorkerReload->fci.params = _main_worker->_This;
			if (call_closure_func(_main_worker->onWorkerReload) != SUCCESS) {
				php_error_docref(NULL, E_ERROR, "call onWorkerStart error");
				return;
			}
		}

		if (_main_worker->reloadable) {
			_main_worker->_status = WM_WORKER_STATUS_RELOADING;
			_unlisten(_main_worker);
			while (1) {
				int coro_num = wmCoroutine_getTotalNum();
				if (coro_num <= reload_coro_num) {
					wmWorker_stopAll();
					break;
				}
				wmCoroutine_sleep(0.1);
			}
		}
	}
}

/**
 * 信号处理函数
 */
void signalHandler(int signal) {
	switch (signal) {
	case SIGINT: // Stop.
		wmWorker_stopAll();
		break;
	case SIGUSR1: // Reload.
		getAllWorkerPids();
		wmArray_clear(_pidsToReload);
		for (int i = 0; i < _pid_array_tmp->offset; i++) {
			int *_pid = wmArray_find(_pid_array_tmp, i);
			wmArray_add(_pidsToReload, _pid);
		}
		reload();
		break;
	case SIGUSR2:		// Show status.
		writeStatisticsToStatusFile();
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

/**
 * 使用epoll重新处理信号
 */
void reinstallSignal() {
	// 忽略 stop
	signal(SIGINT, SIG_IGN);
	// 忽略 reload
	signal(SIGUSR1, SIG_IGN);
	// 忽略 status
	signal(SIGUSR2, SIG_IGN);

	wmSignal_add(SIGINT, signalHandler);
	wmSignal_add(SIGUSR1, signalHandler);
	wmSignal_add(SIGUSR2, signalHandler);

	//创建signal_wait协程 start
	zend_fcall_info_cache signal_wait;
	wm_get_internal_function(NULL, workerman_coroutine_ce_ptr, ZEND_STRL("signal_wait"), &signal_wait);
	wmCoroutine_create(&signal_wait, 0, NULL);
	//创建signal_wait协程 end
}

/**
 * 获得当前worker
 */
wmWorker* wmWorker_getCurrent() {
	return _main_worker;
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
		wmWorker *worker = wmHash_value(_workers, k);
		int count_space_num = worker->count > 9 ? 2 : 1;
		count_space_num = worker->count > 99 ? 3 : count_space_num;
		count_space_num = worker->count > 999 ? 4 : count_space_num;

		echoWin("%-*s%-*s%-*s%d%-*s<g>[OK]</g>\n", //
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
 * 存入status信息
 */
void writeStatisticsToStatusFile() {
	/**
	 * 主进程处理
	 */
	if (_masterPid == getpid()) {
		int ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size,
			"---------------------------------------<w>GLOBAL STATUS</w>--------------------------------------------\n");
		wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack->str, ret, true); //写入PID文件
		ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "WarriorMan version:%s          PHP version:%s\n", PHP_WORKERMAN_VERSION,
		PHP_VERSION);
		wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack->str, ret, true); //写入PID文件

		struct tm *timeinfo = localtime(&_start_timestamp);
		strftime(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%Y-%m-%d %H:%M:%S", timeinfo);
		ret = wm_snprintf(WorkerG.buffer_stack_large->str, WorkerG.buffer_stack_large->size, "start time:%s   run %d days %d hours\n",
			WorkerG.buffer_stack->str, 0, 0);
		wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack_large->str, ret, true); //写入PID文件

		// Workers
		getAllWorkerPids();
		ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%d workers %d processes\n", _workers->size, _pid_array_tmp->offset);
		wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack->str, ret, true); //写入PID文件

		// Process
		ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size,
			"---------------------------------------<w>PROCESS STATUS</w>-------------------------------------------\n");
		wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack->str, ret, true); //写入PID文件

		ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%-*s%-*s%-*s%-*s%-*s%-*s\n", //
			8, "pid", 12, "php_memory", //
			((int) (_maxSocketNameLength - strlen("listening"))) > 0 ? _maxSocketNameLength + 2 : (strlen("listening") + 2), "listening",  //
			((int) (_maxWorkerNameLength - strlen("worker_name"))) > 0 ? _maxWorkerNameLength + 2 : (strlen("worker_name") + 2), "worker_name", //
			13, "connections", 15, "total_request" //
			);//
		wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack->str, ret, true); //写入PID文件
		chmod(_statisticsFile->str, 0722);
		getAllWorkerPids();
		for (int i = 0; i < _pid_array_tmp->offset; i++) {
			int *pid = wmArray_find(_pid_array_tmp, i);
			kill(*pid, SIGUSR2);
		}
		return;
	}
	/**
	 * 子进程处理
	 */
	char _pid[10];
	wm_itoa(_pid, getpid());
	char _memory[10];
	double _mem = ((double) zend_memory_usage(true)) / (1024 * 1024);
	wm_snprintf(_memory, 10, "%.2f", _mem);
	char _conn_num[10];
	wm_itoa(_conn_num, wmConnection_getConnectionsNum());
	char _total_request_num[10];
	wm_itoa(_total_request_num, wmConnection_getTotalRequestNum());

	int ret = wm_snprintf(WorkerG.buffer_stack->str, WorkerG.buffer_stack->size, "%-*s%-*s%-*s%-*s%-*s%-*s\n", //
		8, _pid, 12, _memory, //
		((int) (_maxSocketNameLength - strlen("listening"))) > 0 ? _maxSocketNameLength + 2 : (strlen("listening") + 2), _main_worker->socketName->str,  //
		((int) (_maxWorkerNameLength - strlen("worker_name"))) > 0 ? _maxWorkerNameLength + 2 : (strlen("worker_name") + 2), _main_worker->name->str, //
		13, _conn_num, 15, _total_request_num //
		);//
	wm_file_put_contents(_statisticsFile->str, WorkerG.buffer_stack->str, ret, true); //写入PID文件
}

/**
 * 只向窗口输出
 */
void echoWin(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int retval = vsnprintf(WorkerG.buffer_stack_large->str, WorkerG.buffer_stack_large->size, format, args);
	va_end(args);
	if (retval < 0) {
		retval = 0;
		WorkerG.buffer_stack_large->str[0] = '\0';
	} else if (retval >= WorkerG.buffer_stack_large->size) {
		retval = WorkerG.buffer_stack_large->size - 1;
		WorkerG.buffer_stack_large->str[retval] = '\0';
	}
	WorkerG.buffer_stack_large->length = retval;
	wmString_replace(WorkerG.buffer_stack_large, "<n>", "\033[1A\n\033[K");
	wmString_replace(WorkerG.buffer_stack_large, "<w>", "\033[47;30m");
	wmString_replace(WorkerG.buffer_stack_large, "<g>", "\033[32;40m");
	wmString_replace(WorkerG.buffer_stack_large, "</n>", "\033[0m");
	wmString_replace(WorkerG.buffer_stack_large, "</w>", "\033[0m");
	wmString_replace(WorkerG.buffer_stack_large, "</g>", "\033[0m");

	//标准输出
	fputs(WorkerG.buffer_stack_large->str, stdout);
	fflush(stdout); //刷新缓冲区
}

void _log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = vsnprintf(WorkerG.buffer_stack_large->str, WorkerG.buffer_stack_large->size, format, args);
	va_end(args);
	WorkerG.buffer_stack_large->length = ret;
	wmString_append_ptr(WorkerG.buffer_stack_large, "\n", 2);

	if (!_daemonize) {
		echoWin("%s", WorkerG.buffer_stack_large->str);
	}
	char date[128];
	wm_get_date(date, sizeof(date));

	ret = wm_snprintf(WorkerG.buffer_stack_large->str, WorkerG.buffer_stack_large->size, "%s pid:%d %s", date, getpid(), WorkerG.buffer_stack_large->str);
	wm_file_put_contents(_logFile->str, WorkerG.buffer_stack_large->str, ret, true); //写入PID文件
}

/**
 * 设置运行组和用户
 */
void setUserAndGroup(wmWorker *worker) {
	struct passwd *pw;
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

void wmWorker_free(wmWorker *worker) {
	_unlisten(worker);
	if (worker->socketName != NULL) {
		wmString_free(worker->socketName);
	}
	efree(worker->_This);
	if (worker->host != NULL) {
		efree(worker->host);
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
	if (worker->socket) {
		wmSocket_free(worker->socket);
	}
	//删除并且释放所有连接
	wmConnection_closeConnections();

	//从workers中删除
	WM_HASH_DEL(WM_HASH_INT_STR, _workers, worker->workerId);
	WM_HASH_DEL(WM_HASH_INT_STR, _fd_workers, worker->fd);

	wm_free(worker);
	worker = NULL;
}

void wmWorker_shutdown() {
	//初始化进程标题
	wmString_free(_processTitle);
	wmHash_destroy(WM_HASH_INT_STR,_workers);
	wmHash_destroy(WM_HASH_INT_STR,_fd_workers);
	wmHash_destroy(WM_HASH_INT_STR,_worker_pids);
	wmArray_free(_pid_array_tmp);
}
