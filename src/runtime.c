#include "runtime.h"

typedef struct{
	php_netstream_data_t stream;
	Socket *socket;
} php_study_netstream_data_t;

/**
 * 用来创建php_stream的
 */
php_stream *socket_create(const char *proto, size_t protolen, const char *resourcename, size_t resourcenamelen, //
	const char *persistent_id, int options, int flags, struct timeval *timeout, php_stream_context *context STREAMS_DC
	) {
	php_stream *stream;
	php_study_netstream_data_t *abstract;
	Socket *sock;

	sock = new Socket(AF_INET, SOCK_STREAM, 0);
	abstract = (php_study_netstream_data_t*) ecalloc(1, sizeof(*abstract));
	abstract->socket = sock;
	abstract->stream.socket = sock->get_fd();

	if (timeout) {
		abstract->stream.timeout = *timeout;
	} else {
		abstract->stream.timeout.tv_sec = -1;
	}

	persistent_id = nullptr;
	stream = php_stream_alloc_rel(NULL, abstract, persistent_id, "r+");
	if (stream == NULL) {
		delete sock;
	}
	return stream;
}
