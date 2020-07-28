#include "connection.h"
#include "coroutine.h"
#include "loop.h"

static long wm_coroutine_socket_last_id = 0;
static wmHash_INT_PTR *wm_connections = NULL; //记录着正在连接状态的conn
static wmString *_read_buffer_tmp = NULL; //每次read都从这里中转
static unsigned long total_request = 0; //处理消息总数
static socklen_t addr_len = sizeof(struct sockaddr);

//检查是否发送缓存区慢
static void bufferWillFull(void *_connection);
static void onError(wmConnection *connection);

/**
 * 检查read连接状态
 */
bool check_read_status(wmConnection *connection) {
	if (connection->worker->_status != WM_WORKER_STATUS_RUNNING || connection->_status != WM_CONNECTION_STATUS_ESTABLISHED) {
		return false;
	}
	if (connection->_isPaused) {
		connection->_pausedCoro = wmCoroutine_get_current();
		wmCoroutine_yield();
		connection->_pausedCoro = NULL;
		if (connection->_status == WM_CONNECTION_STATUS_CLOSED) {
			return false;
		}
	}
	return true;
}

void wmConnection_init() {
	wm_connections = wmHash_init(WM_HASH_INT_STR);
	_read_buffer_tmp = wmString_new(WM_BUFFER_SIZE_BIG);
}

wmConnection* wmConnection_create(wmSocket *socket) {
	//查找并且删除key为fd的连接
	WM_HASH_DEL(WM_HASH_INT_STR, wm_connections, socket->fd);
	wmConnection *connection = (wmConnection*) wm_malloc(sizeof(wmConnection));
	connection->fd = socket->fd;
	connection->socket = socket;
	connection->socket->owner = (void*) connection;
	if (connection->socket == NULL) {
		wm_free(connection);
		return NULL;
	}
	connection->transport = socket->transport;
	connection->maxPackageSize = WM_MAX_PACKAGE_SIZE;
	connection->maxSendBufferSize = WM_MAX_SEND_BUFFER_SIZE;
	connection->socket->maxSendBufferSize = connection->maxSendBufferSize;
	connection->socket->onBufferWillFull = bufferWillFull;
	//绑定Full回调

	connection->id = ++wm_coroutine_socket_last_id;
	connection->_status = WM_CONNECTION_STATUS_ESTABLISHED;

	connection->onMessage = NULL;
	connection->onClose = NULL;
	connection->onBufferFull = NULL;
	connection->onBufferDrain = NULL;
	connection->onError = NULL;
	connection->_isPaused = false;
	connection->_pausedCoro = NULL;

	connection->read_packet_buffer = NULL;
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
wmConnection* wmConnection_create_udp(int fd) {
	//新建一个conn
	wmConnection *connection = (wmConnection*) wm_malloc(sizeof(wmConnection));
	connection->fd = fd;
	connection->socket = wmSocket_pack(0, WM_SOCK_UDP, WM_LOOP_SEMI_AUTO);
	connection->transport = WM_SOCK_UDP;
	connection->id = ++wm_coroutine_socket_last_id;
	connection->_status = WM_CONNECTION_STATUS_ESTABLISHED;
	connection->onMessage = NULL;
	connection->onError = NULL;
	connection->onClose = NULL;
	connection->read_packet_buffer = NULL;
	connection->_isPaused = false;
	connection->_pausedCoro = NULL;
	if (connection->id < 0) {
		wm_coroutine_socket_last_id = 0;
		connection->id = ++wm_coroutine_socket_last_id;
	}
	return connection;
}

wmConnection* wmConnection_find_by_fd(int fd) {
	return WM_HASH_GET(WM_HASH_INT_STR, wm_connections, fd);
}

/**
 * onMessage协程结束调用
 */
void onMessage_callback(void *_mess_data) {
	zval *md = (zval*) _mess_data;
	zval *md2 = (zval*) ((char*) _mess_data + sizeof(zval));
	zval_ptr_dtor(md2);
	efree(md);
}

void onMessage_callback_udp(void *_mess_data) {
	zval *md = (zval*) _mess_data;
	zval *md2 = (zval*) ((char*) _mess_data + sizeof(zval));
	zval_ptr_dtor(md2);
	wmConnectionObject *co = wm_connection_fetch_object(Z_OBJ_P(md));
	wmConnection_destroy(co->connection);
	efree(md);
}

/**
 * onError结束调用
 */
void onError_callback(void *_mess_data) {
	zval *md = (zval*) _mess_data;
	zval *md2 = (zval*) ((char*) _mess_data + (sizeof(zval) * 2));
	zval_ptr_dtor(md2);
	efree(md);
}

/**
 * 开始读消息
 * 在一个新协程环境运行
 */
void wmConnection_read(wmConnection *connection) {
	zval z1;
	zval retval_ptr;
	//开始读消息
	while (check_read_status(connection)) {
		///////////////////
		int ret = wmSocket_read(connection->socket, _read_buffer_tmp->str, _read_buffer_tmp->size, WM_SOCKET_MAX_TIMEOUT);
		//触发onError
		if (ret == WM_SOCKET_ERROR) {
			onError(connection);
			return;
		}
		//触发destroy
		if (ret == WM_SOCKET_CLOSE) {
			wmConnection_destroy(connection);
			return;
		}
		/**
		 * 如果只是单纯的tcp协议
		 */
		wmWorker *worker = connection->worker;

		if (worker->protocol) {
			wmString *read_packet_buffer = connection->read_packet_buffer;
			if (read_packet_buffer == NULL) {
				read_packet_buffer = wmString_new(ret);
			}
			wmString_append_ptr(read_packet_buffer, _read_buffer_tmp->str, ret);

			/**
			 * 在这里不断的判断是否是整包
			 */
			while ((read_packet_buffer->length - read_packet_buffer->offset) > 0 && check_read_status(connection)) {
				/**
				 * 调用input
				 */
				ZVAL_STR(&z1,
					zend_string_init((read_packet_buffer->str + read_packet_buffer->offset), read_packet_buffer->length - read_packet_buffer->offset, 0));
				//调用协议的input方法
				zend_call_method(NULL, worker->protocol_ce, NULL, ZEND_STRL("input"), &retval_ptr, 2, &z1, &connection->_This);
				zval_ptr_dtor(&z1);

				if (UNEXPECTED(EG(exception))) {
					zend_exception_error(EG(exception), E_ERROR);
				}

				//判断是否是一个完整的协议包
				if (Z_TYPE(retval_ptr) == IS_LONG) { //判断是否返回的是数字
					zend_long packet_len = Z_LVAL(retval_ptr);
					if (packet_len == 0) {
						break;
					}
					if (packet_len > connection->maxPackageSize) {
						wmWarn("Error package. package_length=%ld", packet_len);
						wmConnection_destroy(connection);
						return;
					}

					//创建一个单独协程处理包
					total_request++;
					if (connection->onMessage) {
						//解码
						ZVAL_STR(&z1, zend_string_init((read_packet_buffer->str + read_packet_buffer->offset), packet_len, 0));
						zend_call_method(NULL, worker->protocol_ce, NULL, ZEND_STRL("decode"), &retval_ptr, 2, &z1, &connection->_This);
						zval_ptr_dtor(&z1);

						//构建zval，默认的引用计数是1，在php方法调用完毕释放
						zval *_mess_data = (zval*) emalloc(sizeof(zval) * 2);
						ZVAL_COPY_VALUE(_mess_data, &connection->_This);
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
			continue;
		}

		if (!check_read_status(connection)) {
			return;
		}

		total_request++;
		//创建一个单独协程处理
		if (connection->onMessage) {
			//构建zval，默认的引用计数是1，在php方法调用完毕释放
			zval *_mess_data = (zval*) emalloc(sizeof(zval) * 2);
			ZVAL_COPY_VALUE(_mess_data, &connection->_This);
			zend_string *_zs = zend_string_init(_read_buffer_tmp->str, ret, 0);
			ZVAL_STR(&_mess_data[1], _zs);
			long _cid = wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
			wmCoroutine_set_callback(_cid, onMessage_callback, _mess_data);
		}
		continue;
	}
}

/**
 * udp读消息
 */
void wmConnection_recvfrom(wmConnection *connection, wmSocket *socket) {
	int ret = wmSocket_recv(socket, connection->socket, _read_buffer_tmp->str, _read_buffer_tmp->size, WM_SOCKET_MAX_TIMEOUT);
	total_request++;
	if (connection->onMessage) {
		//构建zval，默认的引用计数是1，在php方法调用完毕释放
		zval *_mess_data = (zval*) emalloc(sizeof(zval) * 2);
		ZVAL_COPY_VALUE(_mess_data, &connection->_This);
		zend_string *_zs = zend_string_init(_read_buffer_tmp->str, ret, 0);
		ZVAL_STR(&_mess_data[1], _zs);
		long _cid = wmCoroutine_create(&(connection->onMessage->fcc), 2, _mess_data); //创建新协程
		wmCoroutine_set_callback(_cid, onMessage_callback_udp, _mess_data);
	}
}

/**
 * 发送数据
 */
bool wmConnection_send(wmConnection *connection, const void *buf, size_t len, bool raw) {
	if (connection->transport == WM_SOCK_TCP) {
		if (connection->_status != WM_CONNECTION_STATUS_ESTABLISHED) {
			return false;
		}
		int ret;
		zval retval_ptr;
		zval z1;
		//使用协议包装
		if (connection->worker->protocol && raw == false) {
			ZVAL_STR(&z1, zend_string_init(buf, len, 0));
			//调用协议的input方法
			zend_call_method(NULL, connection->worker->protocol_ce, NULL, ZEND_STRL("encode"), &retval_ptr, 2, &z1, &connection->_This);
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
		//触发wmConnection_destroy
		if (ret == WM_SOCKET_CLOSE) {
			wmConnection_destroy(connection);
			return false;
		}
		return true;
	}
	if (connection->transport == WM_SOCK_UDP) {
		int ret = sendto(connection->fd, buf, len, 0, connection->socket->udp_addr, addr_len);
		if (ret < 0) {
			return false;
		}
		return true;
	}
	return false;
}

/**
 * Remove $length of data from receive buffer.
 */
void wmConnection_consumeRecvBuffer(wmConnection *connection, zend_long length) {
	if (connection->read_packet_buffer && (connection->read_packet_buffer->length - connection->read_packet_buffer->offset) > length) {
		connection->read_packet_buffer->length = connection->read_packet_buffer->length - length;
		connection->read_packet_buffer->str[connection->read_packet_buffer->length] = '\0';
	}
}

//应用层发送缓冲区是否这次添加之后，已经满了
void bufferWillFull(void *_connection) {
	wmConnection *connection = (wmConnection*) _connection;
	if (connection->onBufferFull) {
		wmCoroutine_create(&(connection->onBufferFull->fcc), 1, &connection->_This); //创建新协程
	}
}

/**
 * 关闭&删除所有的连接
 */
void wmConnection_closeConnections() {
	for (int k = wmHash_begin(wm_connections); k != wmHash_end(wm_connections); k++) {
		if (!wmHash_exist(wm_connections, k)) {
			continue;
		}
		wmConnection *conn = wmHash_value(wm_connections, k);
		wmHash_del(WM_HASH_INT_STR, wm_connections, k);
		wmConnection_destroy(conn);
	}
}

/**
 * 获取wm_connections的数量
 */
long wmConnection_getConnectionsNum() {
	return wm_connections->size;
}

/**
 * 获取total_request
 */
unsigned long wmConnection_getTotalRequestNum() {
	return total_request;
}

/**
 * 获取对端IP
 */
char* wmConnection_getRemoteIp(wmConnection *connection) {
	return connection->socket->remoteIp->str;
}

/**
 * 获取对端端口
 */
int wmConnection_getRemotePort(wmConnection *connection) {
	return connection->socket->remotePort;
}

/**
 * 停止接收数据
 */
void wmConnection_pauseRecv(wmConnection *connection) {
	connection->_isPaused = true;
}

/**
 * 恢复接收数据
 */
void wmConnection_resumeRecv(wmConnection *connection) {
	connection->_isPaused = false;
	if (connection->_pausedCoro) {
		wmCoroutine_resume(connection->_pausedCoro);
	}
}

/**
 * 处理epoll失败的情况
 */
void onError(wmConnection *connection) {
	wmWarn("onError: (fd=%d,errno %d) %s", connection->fd, connection->socket->errCode, connection->socket->errMsg);

	if (connection->socket && connection->socket->errCode) {
		zend_update_property_long(workerman_connection_ce_ptr, &connection->_This, ZEND_STRL("errCode"), connection->socket->errCode);
		zend_update_property_string(workerman_connection_ce_ptr, &connection->_This, ZEND_STRL("errMsg"), connection->socket->errMsg);
	}

	if (connection->onError) {
		//构建zval，默认的引用计数是1，在php方法调用完毕释放
		zval *_mess_data = (zval*) emalloc(sizeof(zval) * 3);
		ZVAL_COPY_VALUE(_mess_data, &connection->_This);
		ZVAL_LONG(&_mess_data[1], connection->socket->errCode);
		ZVAL_STR(&_mess_data[2], zend_string_init(connection->socket->errMsg, strlen(connection->socket->errMsg), 0));
		long _cid = wmCoroutine_create(&(connection->onError->fcc), 3, _mess_data); //创建新协程
		wmCoroutine_set_callback(_cid, onError_callback, _mess_data);
	}

	if (connection->socket->closed) {
		wmConnection_destroy(connection);
	}
}

/**
 * 直接关闭这个连接
 */
int wmConnection_destroy(wmConnection *connection) {
	if ((!connection) || connection->_status == WM_CONNECTION_STATUS_CLOSED) {
		return 0;
	}
	connection->_status = WM_CONNECTION_STATUS_CLOSED;
	//先设置为false，省的又暂停了
	connection->_isPaused = false;
	int ret = wmSocket_close(connection->socket);
	//开始恢复被暂停的协程
	if (connection->_pausedCoro) {
		wmCoroutine_resume(connection->_pausedCoro);
	}
	//触发onClose
	if (connection->onClose) {
		wmCoroutine_create(&(connection->onClose->fcc), 1, &connection->_This); //创建新协程
	}

	//从connections数组中删除
	if (connection->transport == WM_SOCK_TCP) {
		zend_hash_index_del(Z_ARR(connection->worker->connections), connection->id);
	}

	//查找并且删除key为fd的连接
	WM_HASH_DEL(WM_HASH_INT_STR, wm_connections, connection->fd);
	//释放connection,摧毁这个类，如果顺利的话会触发wmConnection_free
	zval_ptr_dtor(&connection->_This);
	return ret;
}

//释放obj和connection的内存
void wmConnection_free(wmConnection *connection) {
	if (!connection) {
		return;
	}
	//如果还在连接，那么调用close
	if (connection->_status == WM_CONNECTION_STATUS_ESTABLISHED) {
		wmConnection_destroy(connection);
	}
	if (connection->socket) {
		wmSocket_free(connection->socket);
	}
	if (connection->read_packet_buffer) {
		wmString_free(connection->read_packet_buffer);
	}
	wm_free(connection);	//释放connection
	connection = NULL;
}

void wmConnection_shutdown() {
	wmHash_destroy(WM_HASH_INT_STR,wm_connections);
	wmString_free(_read_buffer_tmp);
}
