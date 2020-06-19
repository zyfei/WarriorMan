#include "runtime.h"

typedef struct {
	php_netstream_data_t stream;
	wmSocket* socket;
} php_wm_netstream_data_t;

static int socket_close(php_stream *stream, int close_handle) {
	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	wmSocket* socket = abstract->socket;
	wmSocket_free(socket);
	efree(abstract);
	return 0;
}

static int php_wm_tcp_sockop_bind(php_stream *stream, php_wm_netstream_data_t *abstract, php_stream_xport_param *xparam) {
	char *host = NULL;
	int portno;
	php_printf("xp= %s", xparam->inputs.name);
	host = parse_ip_address(xparam, &portno);
	if (host == NULL) {
		return -1;
	}
	int ret = wm_socket_bind(abstract->socket->fd, host, portno);
	if (host) {
		efree(host);
	}
	return ret;
}

static int socket_set_option(php_stream *stream, int option, int value, void *ptrparam) {
	php_wm_netstream_data_t *abstract = (php_wm_netstream_data_t *) stream->abstract;
	php_stream_xport_param *xparam;

	switch (option) {
	case PHP_STREAM_OPTION_XPORT_API:
		xparam = (php_stream_xport_param *) ptrparam;

		switch (xparam->op) {
		case STREAM_XPORT_OP_BIND:
			xparam->outputs.returncode = php_wm_tcp_sockop_bind(stream, abstract, xparam);
			return PHP_STREAM_OPTION_RETURN_OK;
		case STREAM_XPORT_OP_LISTEN:
			xparam->outputs.returncode = wm_socket_listen(abstract->socket->fd, xparam->inputs.backlog);
			return PHP_STREAM_OPTION_RETURN_OK;
		default:
			/* fall through */
			;
		}
	}
	return PHP_STREAM_OPTION_RETURN_OK;
}

static php_stream_ops tcp_socket_ops = { //
	NULL, NULL, socket_close, NULL, //
		"tcp_socket/coroutine", //
		NULL, /* seek */
		NULL, //
		NULL, //
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

	sock = wmSocket_create(AF_INET, SOCK_STREAM, 0);
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
