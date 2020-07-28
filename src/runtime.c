#include "runtime.h"
#include "wm_socket.h"

typedef struct {
	php_netstream_data_t stream;
	wmSocket* socket;
} php_wm_netstream_data_t;

#if PHP_VERSION_ID < 70400
#define RUNTIME_SIZE_T size_t
#else
#define RUNTIME_SIZE_T ssize_t
#endif

void wmRuntime_init() {
}
void wmRuntime_shutdown() {
}
///////

static RUNTIME_SIZE_T socket_write(php_stream *stream, const char *buf, size_t count) {
	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	if (UNEXPECTED(!abstract)) {
		return 0;
	}
	wmSocket *sock = abstract->socket;

	ssize_t didwrite;
	if (UNEXPECTED(!sock)) {
		return 0;
	}
	didwrite = wmSocket_write(sock, buf, count);
	if (didwrite > 0) {
		php_stream_notify_progress_increment(PHP_STREAM_CONTEXT(stream), didwrite, 0);
	}
#if PHP_VERSION_ID < 70400
	if (didwrite < 0) {
		didwrite = 0;
	}
#endif
	return didwrite;
}

static RUNTIME_SIZE_T socket_read(php_stream *stream, char *buf, size_t count) {
	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	if (UNEXPECTED(!abstract)) {
		return 0;
	}
	wmSocket *sock = abstract->socket;

	ssize_t nr_bytes = 0;
	if (UNEXPECTED(!sock)) {
		return 0;
	}
	nr_bytes = wmSocket_read(sock, buf, count, sock->read_timeout);
	if (nr_bytes == WM_SOCKET_ERROR || nr_bytes == WM_SOCKET_CLOSE) {
		stream->eof = 1;
	} else {
		stream->eof = 0;
	}

	if (nr_bytes > 0) {
		php_stream_notify_progress_increment(PHP_STREAM_CONTEXT(stream), nr_bytes, 0);
	}
#if PHP_VERSION_ID < 70400
	if (nr_bytes < 0) {
		nr_bytes = 0;
	}
#endif
	return nr_bytes;
}

static int socket_close(php_stream *stream, int close_handle) {
	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	wmSocket* sock = abstract->socket;
	wmSocket_free(sock);
	efree(abstract);
	return 0;
}

static int socket_flush(php_stream *stream) {
	return 0;
}

static int socket_cast(php_stream *stream, int castas, void **ret) {

	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	if (UNEXPECTED(!abstract)) {
		return FAILURE;
	}
	wmSocket* sock = abstract->socket;
	if (UNEXPECTED(!sock)) {
		return FAILURE;
	}

	switch (castas) {
	case PHP_STREAM_AS_STDIO:
		if (ret) {
			*(FILE**) ret = fdopen(sock->fd, stream->mode);
			if (*ret) {
				return SUCCESS;
			}
			return FAILURE;
		}
		return SUCCESS;
	case PHP_STREAM_AS_FD_FOR_SELECT:
	case PHP_STREAM_AS_FD:
	case PHP_STREAM_AS_SOCKETD:
		if (ret)
			*(php_socket_t *) ret = sock->fd;
		return SUCCESS;
	default:
		return FAILURE;
	}
}

static int socket_stat(php_stream *stream, php_stream_statbuf *ssb) {

	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	if (UNEXPECTED(!abstract)) {
		return FAILURE;
	}
	wmSocket* sock = abstract->socket;
	if (UNEXPECTED(!sock)) {
		return FAILURE;
	}
	return zend_fstat(sock->fd, &ssb->sb);
}

static int socket_bind(php_stream *stream, wmSocket *sock, php_stream_xport_param *xparam) {

	char *host = NULL;
	int portno;
	host = parse_ip_address(xparam, &portno);
	if (host == NULL) {
		return -1;
	}
	int ret = wm_socket_bind(sock->fd, host, portno);
	if (host) {
		efree(host);
	}
	return ret;
}

/**
 * php socket的connect方法
 */
static int socket_connect(php_stream *stream, wmSocket *sock, php_stream_xport_param *xparam) {
	php_printf("socket_connect\n");
	char *host = NULL;
	int portno = 0;
	int ret = 0;
	char *ip_address = NULL;
	if (!sock) {
		return FAILURE;
	}
	if (sock->transport == WM_SOCK_TCP || sock->transport == WM_SOCK_UDP) {
		ip_address = parse_ip_address_ex(xparam->inputs.name, xparam->inputs.namelen, &portno, xparam->want_errortext, &xparam->outputs.error_text);
		host = ip_address;
		if (sock->transport == WM_SOCK_TCP) { //SOCK_STREAM
			int sockoptval = 1;
			setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (char*) &sockoptval, sizeof(sockoptval));
		}
	} else {
		host = xparam->inputs.name;
	}
	if (host == NULL) {
		return FAILURE;
	}
	uint32_t timeout = WM_SOCKET_DEFAULT_CONNECT_TIMEOUT;
	if (xparam->inputs.timeout) { //暂时没有超时设置
		timeout = xparam->inputs.timeout->tv_sec * 1000 + (uint32_t) xparam->inputs.timeout->tv_usec / 1000;
	}
	if (wmSocket_connect(sock, host, portno, timeout) == false) {
		xparam->outputs.error_code = sock->errCode;
		if (sock->errMsg) {
			xparam->outputs.error_text = zend_string_init(sock->errMsg, strlen(sock->errMsg), 0);
		}
		ret = -1;
	}
	if (ip_address) {
		efree(ip_address);
	}
	return ret;
}

static inline int socket_accept(php_stream *stream, wmSocket *sock, php_stream_xport_param *xparam STREAMS_DC) {
	int tcp_nodelay = 0;
	zval *tmpzval = NULL;

	xparam->outputs.client = NULL;

	if ((NULL != PHP_STREAM_CONTEXT(stream))
		&& (tmpzval = php_stream_context_get_option(PHP_STREAM_CONTEXT(stream), "socket", "tcp_nodelay")) != NULL&& zval_is_true(tmpzval)) {
		tcp_nodelay = 1;
	}

	zend_string **textaddr = xparam->want_textaddr ? &xparam->outputs.textaddr : NULL;
	struct sockaddr **addr = xparam->want_addr ? &xparam->outputs.addr : NULL;
	socklen_t *addrlen = xparam->want_addr ? &xparam->outputs.addrlen : NULL;

	struct timeval *timeout = xparam->inputs.timeout;
	zend_string **error_string = xparam->want_errortext ? &xparam->outputs.error_text : NULL;
	int *error_code = &xparam->outputs.error_code;

	int error = 0;
	php_sockaddr_storage sa;
	socklen_t sl = sizeof(sa);

	uint32_t timeout2 = WM_SOCKET_DEFAULT_CONNECT_TIMEOUT;
	if (timeout) {
		timeout2 = timeout->tv_sec * 1000 + (uint32_t) timeout->tv_usec / 1000;
	}
	wmSocket* clisock = wmSocket_accept(sock, WM_LOOP_AUTO, timeout2);

	if (clisock == NULL) {
		error = sock->errCode;
		if (error_code) {
			*error_code = error;
		}
		if (error_string) {
			*error_string = php_socket_error_str(error);
		}
		return FAILURE;
	} else {
		php_network_populate_name_from_sockaddr((struct sockaddr*) &sa, sl, textaddr, addr, addrlen);
#ifdef TCP_NODELAY
		if (tcp_nodelay)
		{
			setsockopt(clisock->fd, IPPROTO_TCP, TCP_NODELAY, (char*) &tcp_nodelay, sizeof(tcp_nodelay));
		}
#endif
		php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t*) emalloc(sizeof(*abstract));
		memset(abstract, 0, sizeof(*abstract));

		abstract->socket = clisock;

		xparam->outputs.client = php_stream_alloc_rel(stream->ops, (void* )abstract, NULL, "r+");
		if (xparam->outputs.client) {
			xparam->outputs.client->ctx = stream->ctx;
			if (stream->ctx) {
				GC_ADDREF(stream->ctx);
			}
		}
		return 0;
	}
}

static inline int socket_sendto(wmSocket *sock, const char *buf, size_t buflen, struct sockaddr *addr, socklen_t addrlen) {
	if (addr) {
		return sendto(sock->fd, buf, buflen, 0, addr, addrlen);
	} else {
		return wmSocket_send(sock, buf, buflen);
	}
}

static inline int socket_recvfrom(wmSocket *sock, char *buf, size_t buflen, zend_string **textaddr, struct sockaddr **addr, socklen_t *addrlen) {
	int ret;
	int want_addr = textaddr || addr;

	if (want_addr) {
		php_sockaddr_storage sa;
		socklen_t sl = sizeof(sa);

		ret = wmSocket_recvfrom(sock, buf, buflen, (struct sockaddr*) &sa, &sl,sock->read_timeout);
		if (sl) {
			php_network_populate_name_from_sockaddr((struct sockaddr*) &sa, sl, textaddr, addr, addrlen);
		} else {
			if (textaddr) {
				*textaddr = ZSTR_EMPTY_ALLOC();
			}
			if (addr) {
				*addr = NULL;
				*addrlen = 0;
			}
		}
	} else {
		ret = wmSocket_read(sock, buf, buflen, 1000);
	}
	return ret;
}

static inline int socket_xport_api(php_stream *stream, wmSocket *sock, php_stream_xport_param *xparam STREAMS_DC) {
	static const int shutdown_how[] = { SHUT_RD, SHUT_WR, SHUT_RDWR };
	switch (xparam->op) {
	case STREAM_XPORT_OP_LISTEN: {
		xparam->outputs.returncode = wm_socket_listen(sock->fd, xparam->inputs.backlog) ? 0 : -1; //listen
		break;
	}
	case STREAM_XPORT_OP_CONNECT:
	case STREAM_XPORT_OP_CONNECT_ASYNC:
		xparam->outputs.returncode = socket_connect(stream, sock, xparam); //connect
		break;
	case STREAM_XPORT_OP_BIND: {
		//if (sock->get_sock_domain() != AF_UNIX) {
		zval *tmpzval = NULL;
		int sockoptval = 1;
		php_stream_context *ctx = PHP_STREAM_CONTEXT(stream);
		if (!ctx) {
			break;
		}
#ifdef SO_REUSEADDR
		setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (char*) &sockoptval, sizeof(sockoptval));
#endif

#ifdef SO_REUSEPORT
		if ((tmpzval = php_stream_context_get_option(ctx, "socket", "so_reuseport")) != NULL
			&& zval_is_true(tmpzval))
		{
			setsockopt(sock->fd, SOL_SOCKET, SO_REUSEPORT, (char*) &sockoptval, sizeof(sockoptval));
		}
#endif

#ifdef SO_BROADCAST
		if ((tmpzval = php_stream_context_get_option(ctx, "socket", "so_broadcast")) != NULL
			&& zval_is_true(tmpzval))
		{
			setsockopt(sock->fd, SOL_SOCKET, SO_BROADCAST, (char*) &sockoptval, sizeof(sockoptval));
		}
#endif
		//}
		xparam->outputs.returncode = socket_bind(stream, sock, xparam);
		break;
	}
	case STREAM_XPORT_OP_ACCEPT:
		xparam->outputs.returncode = socket_accept(stream, sock, xparam STREAMS_CC);
		break;
	case STREAM_XPORT_OP_GET_NAME:
		xparam->outputs.returncode = php_network_get_sock_name(sock->fd, xparam->want_textaddr ? &xparam->outputs.textaddr : NULL,
			xparam->want_addr ? &xparam->outputs.addr : NULL, xparam->want_addr ? &xparam->outputs.addrlen : NULL);
		break;
	case STREAM_XPORT_OP_GET_PEER_NAME:
		xparam->outputs.returncode = php_network_get_peer_name(sock->fd, xparam->want_textaddr ? &xparam->outputs.textaddr : NULL,
			xparam->want_addr ? &xparam->outputs.addr : NULL, xparam->want_addr ? &xparam->outputs.addrlen : NULL);
		break;

	case STREAM_XPORT_OP_SEND:
		if ((xparam->inputs.flags & STREAM_OOB) == STREAM_OOB) {
			wmWarn("STREAM_OOB flags is not supports");
			xparam->outputs.returncode = -1;
			break;
		}
		xparam->outputs.returncode = socket_sendto(sock, xparam->inputs.buf, xparam->inputs.buflen, xparam->inputs.addr, xparam->inputs.addrlen);
		if (xparam->outputs.returncode == -1) {
			char *err = php_socket_strerror(php_socket_errno(), NULL, 0);
			php_error_docref(NULL, E_WARNING, "%s\n", err);
			efree(err);
		}
		break;

	case STREAM_XPORT_OP_RECV:
		if ((xparam->inputs.flags & STREAM_OOB) == STREAM_OOB) {
			wmWarn("STREAM_OOB flags is not supports");
			xparam->outputs.returncode = -1;
			break;
		}
		if ((xparam->inputs.flags & STREAM_PEEK) == STREAM_PEEK) {
			xparam->outputs.returncode = wmSocket_peek(sock, xparam->inputs.buf, xparam->inputs.buflen);
		} else {
			xparam->outputs.returncode = socket_recvfrom(sock, xparam->inputs.buf, xparam->inputs.buflen,
				xparam->want_textaddr ? &xparam->outputs.textaddr : NULL, xparam->want_addr ? &xparam->outputs.addr : NULL,
				xparam->want_addr ? &xparam->outputs.addrlen : NULL);
		}
		break;
	case STREAM_XPORT_OP_SHUTDOWN:
		xparam->outputs.returncode = wmSocket_shutdown(sock, shutdown_how[xparam->how]);
		break;
	default:
		wmWarn("debug - >socket_xport_api: unsupported option %d", xparam->op)
		break;
	}
	return PHP_STREAM_OPTION_RETURN_OK;
}

static int socket_set_option(php_stream *stream, int option, int value, void *ptrparam) {
	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	if (UNEXPECTED(!abstract || !abstract->socket)) {
		return PHP_STREAM_OPTION_RETURN_ERR;
	}
	wmSocket* sock = abstract->socket;
	//php_stream_xport_param *xparam;

	switch (option) {
	case PHP_STREAM_OPTION_BLOCKING:
		// The coroutine socket always consistent with the sync blocking socket
		return value ? PHP_STREAM_OPTION_RETURN_OK : PHP_STREAM_OPTION_RETURN_ERR;
	case PHP_STREAM_OPTION_XPORT_API:
		return socket_xport_api(stream, sock, (php_stream_xport_param *) ptrparam STREAMS_CC);
	case PHP_STREAM_OPTION_META_DATA_API: {
		add_assoc_bool((zval * )ptrparam, "timed_out", sock->errCode == ETIMEDOUT);
		add_assoc_bool((zval * )ptrparam, "eof", stream->eof);
		add_assoc_bool((zval * )ptrparam, "blocked", 1);
		break;
	}
	case PHP_STREAM_OPTION_READ_TIMEOUT: {
		struct timeval* _read_timeout = (struct timeval*) ptrparam;
		abstract->socket->read_timeout = _read_timeout->tv_sec * 1000 + (uint32_t) _read_timeout->tv_usec / 1000;
		break;
	}
	case PHP_STREAM_OPTION_CHECK_LIVENESS: {
		php_printf("PHP_STREAM_OPTION_CHECK_LIVENESS ! hai mei you\n");
		return PHP_STREAM_OPTION_RETURN_OK;
		//return sock->check_liveness() ? PHP_STREAM_OPTION_RETURN_OK : PHP_STREAM_OPTION_RETURN_ERR;
	}
	case PHP_STREAM_OPTION_READ_BUFFER:
	case PHP_STREAM_OPTION_WRITE_BUFFER: {
		// TODO: read/write buffer
		break;
	}
	default:
		wmWarn("debug - >socket_set_option: unsupported option %d with value %d", option, value)
		break;
	}
	return PHP_STREAM_OPTION_RETURN_OK;
}

static php_stream_ops tcp_socket_ops = { //
	socket_write, socket_read, socket_close, socket_flush, //
		"tcp_socket/coroutine", //
		NULL, /* seek */
		socket_cast, //
		socket_stat, //
		socket_set_option //
	};

/**
 * 用来创建php_stream的
 */
php_stream *wmRuntime_socket_create(const char *proto, size_t protolen, const char *resourcename, size_t resourcenamelen, //
	const char *persistent_id, int options, int flags, struct timeval *timeout, php_stream_context *context STREAMS_DC
	) {
	php_stream *stream;
	php_wm_netstream_data_t *abstract;
	wmSocket *sock;

	sock = wmSocket_create(WM_SOCK_TCP, WM_LOOP_AUTO);
	if (!sock) {
		return NULL;
	}
	abstract = (php_wm_netstream_data_t*) ecalloc(1, sizeof(*abstract));
	abstract->socket = sock;
	abstract->stream.socket = sock->fd;

	//设置超时时间
	if (timeout) {
		abstract->stream.timeout = *timeout;
	} else {
		abstract->stream.timeout.tv_sec = -1;
	}
	persistent_id = NULL;
	//创建一个php stream
	stream = php_stream_alloc_rel(&tcp_socket_ops, abstract, persistent_id, "r+");
	if (stream == NULL) {
		wmSocket_free(sock);
	}
	return stream;
}
