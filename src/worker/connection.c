#include "connection.h"
#include "coroutine.h"
#include "loop.h"

static long wm_coroutine_socket_last_id = 0;
static wmHash_INT_PTR *wm_connections = NULL; //记录着正在连接状态的conn
static wmString* _read_buffer_tmp = NULL; //每次read都从这里中转

//检查是否发送缓存区慢
static void bufferWillFull(void *_connection);
static int onClose(wmConnection *connection);
static void onError(wmConnection *connection);

void wmConnection_init() {
	wm_connections = wmHash_init(WM_HASH_INT_STR);
	_read_buffer_tmp = wmString_new(WM_BUFFER_SIZE_BIG);
}

wmConnection * wmConnection_create(wmSocket* socket) {
	//查找并且删除key为fd的连接
	WM_HASH_DEL(WM_HASH_INT_STR, wm_connections, socket->fd);
	wmConnection *connection = (wmConnection *) wm_malloc(sizeof(wmConnection));
	connection->fd = socket->fd;
	connection->socket = socket;
	connection->socket->owner = (void *) connection;
	if (connection->socket == NULL) {
		wm_free(connection);
		return NULL;
	}
	connection->transport = socket->transport;
	connection->socket->maxPackageSize = WM_MAX_PACKAGE_SIZE;
	connection->socket->maxSendBufferSize = WM_MAX_SEND_BUFFER_SIZE;
	connection->socket->onBufferWillFull = bufferWillFull;
	//绑定Full回调

	connection->_This = NULL;
	connection->id = ++wm_coroutine_socket_last_id;
	connection->_status = WM_CONNECTION_STATUS_ESTABLISHED;

	connection->maxSendBufferSize = WM_MAX_SEND_BUFFER_SIZE;
	connection->maxPackageSize = WM_MAX_PACKAGE_SIZE;

	connection->onMessage = NULL;
	connection->onClose = NULL;
	connection->onBufferFull = NULL;
	connection->onBufferDrain = NULL;
	connection->onError = NULL;

	connection->read_packet_buffer = NULL;
	connection->addr = NULL;
	if (connection->id < 0) {
		wm_coroutine_socket_last_id = 0;
		connection->id = ++wm_coroutine_socket_last_id;
	}
	if (WM_HASH_ADD(WM_HASH_INT_STR,wm_connections,connection->fd,connection) < 0) {
		wmWarn("wmConnection_create-> connections_add fail");
		return NULL;
	}
	return connection;
}

/**
 * 为udp申请一个连接
 */
wmConnection * wmConnection_create_udp(int fd) {
	//新建一个conn
	wmConnection *connection = (wmConnection *) wm_malloc(sizeof(wmConnection));
	connection->fd = fd;
	connection->socket = NULL;
	connection->transport = WM_SOCK_UDP;
	connection->_This = NULL;
	connection->id = ++wm_coroutine_socket_last_id;
	connection->_status = WM_CONNECTION_STATUS_CLOSED; //默认就是关闭的，因为udp是无连接的
	connection->onMessage = NULL;
	connection->onError = NULL;
	connection->read_packet_buffer = NULL;
	if (connection->id < 0) {
		wm_coroutine_socket_last_id = 0;
		connection->id = ++wm_coroutine_socket_last_id;
	}
	connection->addr = wm_malloc(sizeof(struct sockaddr));
	connection->addr_len = sizeof(struct sockaddr);
	return connection;
}

wmConnection* wmConnection_find_by_fd(int fd) {
	return WM_HASH_GET(WM_HASH_INT_STR, wm_connections, fd);
}

/**
 * onMessage协程结束调用
 */
void onMessage_callback(void* _mess_data) {
	zval* md = (zval*) _mess_data;
	zval* md2 = (zval*) ((char *) _mess_data + sizeof(zval));
	zval_ptr_dtor(md2);
	efree(md);
}

void onMessage_callback_udp(void* _mess_data) {
	zval* md = (zval*) _mess_data;
	zval* md2 = (zval*) ((char *) _mess_data + sizeof(zval));
	zval_ptr_dtor(md2);
	zval_ptr_dtor(md);
	efree(md);
}

/**
 * onError结束调用
 */
void onError_callback(void* _mess_data) {
	zval* md = (zval*) _mess_data;
	zval* md2 = (zval*) ((char *) _mess_data + (sizeof(zval) * 2));
	zval_ptr_dtor(md2);
	efree(md);
}

/**
 * 开始读消息
 * 在一个新协程环境运行
 */
void wmConnection_read(wmConnection* connection) {
	zval z1;
	zval retval_ptr;
	//开始读消息
	while (connection->worker->_status == WM_WORKER_STATUS_RUNNING && connection && connection->_status == WM_CONNECTION_STATUS_ESTABLISHED) {
		int ret = wmSocket_read(connection->socket, _read_buffer_tmp->str, _read_buffer_tmp->size, WM_SOCKET_MAX_TIMEOUT);
		//触发onError
		if (ret == WM_SOCKET_ERROR) {
			onError(connection);
			return;
		}
		//触发onClose
		if (ret == WM_SOCKET_CLOSE) {
			onClose(connection);
			return;
		}
		/**
		 * 如果只是单纯的tcp协议
		 */
		wmWorker* worker = connection->worker;
		if (!worker->protocol) {
			//创建一个单独协程处理
			if (connection->onMessage) {
				//构建zval，默认的引用计数是1，在php方法调用完毕释放
				zval* _mess_data = (zval*) emalloc(sizeof(zval) * 2);
				//_mess_data[0] = *connection->_This;
				ZVAL_COPY_VALUE(_mess_data, connection->_This);
				zend_string* _zs = zend_string_init(_read_buffer_tmp->str, ret, 0);
				ZVAL_STR(&_mess_data[1], _zs);
				long _cid = wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
				wmCoroutine_set_callback(_cid, onMessage_callback, _mess_data);
			}
			continue;
		}

		/**
		 * 如果是http，websocket等高级协议
		 */
		wmString* read_packet_buffer = connection->read_packet_buffer;
		if (read_packet_buffer == NULL) {
			read_packet_buffer = wmString_new(ret);
		}
		wmString_append_ptr(read_packet_buffer, _read_buffer_tmp->str, ret);

		/**
		 * 在这里不断的判断是否是整包
		 */
		while (1) {
			if ((read_packet_buffer->length - read_packet_buffer->offset) <= 0) {
				break;
			}
			/**
			 * 调用input
			 */
			ZVAL_STR(&z1, zend_string_init((read_packet_buffer->str + read_packet_buffer->offset), read_packet_buffer->length - read_packet_buffer->offset, 0));
			//调用协议的input方法
			zend_call_method(NULL, worker->protocol_ce, NULL, ZEND_STRL("input"), &retval_ptr, 2, &z1, connection->_This);
			zval_ptr_dtor(&z1);

			if (UNEXPECTED(EG(exception))) {
				zend_exception_error(EG(exception), E_ERROR);
			}

			//php_var_dump(&retval_ptr,1);
			//判断是否是一个完整的协议包
			if (Z_TYPE(retval_ptr) == IS_LONG) { //判断是否返回的是数字
				zend_long packet_len = Z_LVAL(retval_ptr);
				if (packet_len == 0) {
					break;
				}
				//创建一个单独协程处理包
				if (connection->onMessage) {
					//解码
					ZVAL_STR(&z1, zend_string_init((read_packet_buffer->str + read_packet_buffer->offset), packet_len, 0));
					zend_call_method(NULL, worker->protocol_ce, NULL, ZEND_STRL("decode"), &retval_ptr, 2, &z1, connection->_This);
					zval_ptr_dtor(&z1);

					//构建zval，默认的引用计数是1，在php方法调用完毕释放
					zval* _mess_data = (zval*) emalloc(sizeof(zval) * 2);
					ZVAL_COPY_VALUE(_mess_data, connection->_This);
					ZVAL_COPY_VALUE(&_mess_data[1], &retval_ptr);

					long _cid = wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
					wmCoroutine_set_callback(_cid, onMessage_callback, _mess_data);
				}
				read_packet_buffer->offset += packet_len;
			} else { //其他类型直接协议错误
				zval_ptr_dtor(&retval_ptr);
				wmSocket_close(connection->socket);
				connection->socket->errCode = WM_ERROR_PROTOCOL_FAIL;
				connection->socket->errMsg = wmCode_str(WM_ERROR_PROTOCOL_FAIL);
				onError(connection);
				return;
			}
		}

		//大于0代表有消息被onMessage处理了，重新创建string缓冲区
		if (read_packet_buffer->offset > 0) {
			//重新创建一个read_packet缓冲区
			int residue_buffer_len = read_packet_buffer->length - read_packet_buffer->offset;
			connection->read_packet_buffer = wmString_dup(read_packet_buffer->str + read_packet_buffer->offset, residue_buffer_len);
			wmString_free(read_packet_buffer);
		}
	}
}

/**
 * udp读消息
 */
void wmConnection_recvfrom(wmConnection* connection, wmSocket* socket) {
	int ret = wmSocket_recvfrom(socket, _read_buffer_tmp->str, _read_buffer_tmp->size, connection->addr, &connection->addr_len, WM_SOCKET_MAX_TIMEOUT);
	if (ret == WM_SOCKET_CLOSE) { //关闭了，进程退出的时候触发
		zval_ptr_dtor(connection->_This);
		socket->closed = true;
		return;
	}
	//触发onError
	if (ret <= 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		zval_ptr_dtor(connection->_This);
		return;
	}
	//创建一个单独协程处理
	if (connection->onMessage) {
		//构建zval，默认的引用计数是1，在php方法调用完毕释放
		zval* _mess_data = (zval*) emalloc(sizeof(zval) * 2);
		//_mess_data[0] = *connection->_This;
		//_mess_data[0] = *connection->_This;
		ZVAL_COPY_VALUE(_mess_data, connection->_This);
		zend_string* _zs = zend_string_init(_read_buffer_tmp->str, ret, 0);
		ZVAL_STR(&_mess_data[1], _zs);
		long _cid = wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
		wmCoroutine_set_callback(_cid, onMessage_callback_udp, _mess_data);
	}
}

/**
 * 发送数据
 */
bool wmConnection_send(wmConnection *connection, const void *buf, size_t len) {
	if (connection->transport == WM_SOCK_TCP) {
		if (connection->_status != WM_CONNECTION_STATUS_ESTABLISHED) {
			return false;
		}
		int ret;
		zval retval_ptr;
		zval z1;
		//使用协议包装
		if (connection->worker->protocol) {
			ZVAL_STR(&z1, zend_string_init(buf, len, 0));
			//调用协议的input方法
			zend_call_method(NULL, connection->worker->protocol_ce, NULL, ZEND_STRL("encode"), &retval_ptr, 2, &z1, connection->_This);
			if (UNEXPECTED(EG(exception))) {
				zend_exception_error(EG(exception), E_ERROR);
			}
			if (Z_TYPE(retval_ptr) != IS_STRING) { //判断是否返回的是字符串
				zval_ptr_dtor(&z1);
				zval_ptr_dtor(&retval_ptr);
				wmSocket_close(connection->socket);
				connection->socket->errCode = WM_ERROR_PROTOCOL_FAIL;
				connection->socket->errMsg = wmCode_str(WM_ERROR_PROTOCOL_FAIL);
				onError(connection);
				return false;
			}
			ret = wmSocket_send(connection->socket, retval_ptr.value.str->val, retval_ptr.value.str->len);
			zval_ptr_dtor(&z1);
			zval_ptr_dtor(&retval_ptr);
		} else {
			ret = wmSocket_send(connection->socket, buf, len);
		}

		//触发onError
		if (ret == WM_SOCKET_ERROR) {
			onError(connection);
			return false;
		}
		//触发onClose
		if (ret == WM_SOCKET_CLOSE) {
			onClose(connection);
			return false;
		}
		return true;
	} else if (connection->transport == WM_SOCK_UDP) {
		int ret = sendto(connection->fd, buf, len, 0, connection->addr, connection->addr_len);
		if (ret < 0) {
			return false;
		}
		return true;
	}
	return false;
}

//应用层发送缓冲区是否这次添加之后，已经满了
void bufferWillFull(void *_connection) {
	wmConnection* connection = (wmConnection*) _connection;
	if (connection->onBufferFull) {
		wmCoroutine_create(&(connection->onBufferFull->fcc), 1, connection->_This); //创建新协程
	}
}

//这是一个用户调用的方法
int wmConnection_close(wmConnection *connection) {
	return onClose(connection);
}

/**
 * 关闭&删除所有的连接
 */
void wmConnection_close_connections() {
	for (int k = wmHash_begin(wm_connections); k != wmHash_end(wm_connections); k++) {
		if (!wmHash_exist(wm_connections, k)) {
			continue;
		}
		wmConnection* conn = wmHash_value(wm_connections, k);
		wmHash_del(WM_HASH_INT_STR, wm_connections, k);
		onClose(conn);
	}
}

/**
 * 处理epoll失败的情况
 */
void onError(wmConnection *connection) {
	wmWarn("onError: (fd=%d,errno %d) %s", connection->fd, connection->socket->errCode, connection->socket->errMsg);

	if (connection->socket && connection->socket->errCode) {
		zend_update_property_long(workerman_connection_ce_ptr, connection->_This, ZEND_STRL("errCode"), connection->socket->errCode);
		zend_update_property_string(workerman_connection_ce_ptr, connection->_This, ZEND_STRL("errMsg"), connection->socket->errMsg);
	}

	if (connection->onError) {
		//构建zval，默认的引用计数是1，在php方法调用完毕释放
		zval* _mess_data = (zval*) emalloc(sizeof(zval) * 3);
		ZVAL_COPY_VALUE(_mess_data, connection->_This);
		ZVAL_LONG(&_mess_data[1], connection->socket->errCode);
		ZVAL_STR(&_mess_data[2], zend_string_init(connection->socket->errMsg, strlen(connection->socket->errMsg), 0));
		long _cid = wmCoroutine_create(&(connection->onError->fcc), 3, _mess_data); //创建新协程
		wmCoroutine_set_callback(_cid, onError_callback, _mess_data);
	}

	if (connection->socket->closed) {
		onClose(connection);
	}
}

/**
 * 关闭这个连接
 */
int onClose(wmConnection *connection) {
	if (connection->_status == WM_CONNECTION_STATUS_CLOSED) {
		return 0;
	}
	int ret = wmSocket_close(connection->socket);

	connection->_status = WM_CONNECTION_STATUS_CLOSED;
	//触发onClose
	if (connection->onClose) {
		wmCoroutine_create(&(connection->onClose->fcc), 1, connection->_This); //创建新协程
	}

	//查找并且删除key为fd的连接
	WM_HASH_DEL(WM_HASH_INT_STR, wm_connections, connection->fd);

	//释放connection,摧毁这个类，如果顺利的话会触发wmConnection_free
	zval_ptr_dtor(connection->_This);

	return ret;
}

//释放obj和connection的内存
void wmConnection_free(wmConnection *connection) {
	if (!connection) {
		return;
	}
	//如果还在连接，那么调用close
	if (connection->_status == WM_CONNECTION_STATUS_ESTABLISHED) {
		onClose(connection);
	}
	if (connection->socket) {
		wmSocket_free(connection->socket);
	}
	if (connection->read_packet_buffer) {
		wmString_free(connection->read_packet_buffer);
	}
	//释放暂时申请指向自身php对象的zval指针
	if (connection->_This) {
		efree(connection->_This);
	}
	if (connection->addr) {
		wm_free(connection->addr);
	}
	wm_free(connection);	//释放connection
	connection = NULL;
}

void wmConnection_shutdown() {
	wmHash_destroy(WM_HASH_INT_STR,wm_connections);
	wmString_free(_read_buffer_tmp);
}
