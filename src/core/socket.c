#include "header.h"
#include "log.h"
#include "socket.h"

/**
 * 创建套接字
 */
int wm_socket_create(int domain, int type, int protocol) {
	int sock;
	sock = socket(domain, type, protocol);
	if (sock < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
	}
	return sock;
}

/**
 * 设置为非阻塞模式
 */
int wm_socket_set_nonblock(int sock) {
	int flags;
	//用来获取这个socket原来的一些属性。
	flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return -1;
	}
	//在原来的属性上加上非阻塞的属性。
	flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	if (flags < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * 对bind()函数进行了封装
 */
int wm_socket_bind(int sock, char *host, int port) {
	int ret;
	struct sockaddr_in servaddr;

	//初始化servaddr
	bzero(&servaddr, sizeof(servaddr));
	//将host转换为网络结构体sockaddr_in
	inet_aton(host, &(servaddr.sin_addr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	//把socket和地址，端口绑定
	ret = bind(sock, (struct sockaddr*) &servaddr, sizeof(servaddr));
	if (ret < 0) {
		return -1;
	}

	return ret;
}

/**
 * 对connect进行封装
 */
int wm_socket_connect(int sock, char *host, int port) {
	if (port <= 0 || port >= 65536) {
		wmWarn("Invalid port [%d]", port);
		return -1;
	}

	int ret;
	struct sockaddr_in servaddr;

	//初始化servaddr
	bzero(&servaddr, sizeof(servaddr));
	//将host转换为网络结构体sockaddr_in
	inet_aton(host, &(servaddr.sin_addr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	//把socket和地址，端口绑定
	ret = connect(sock, (struct sockaddr*) &servaddr, sizeof(servaddr));
	if (ret < 0) {
		return -1;
	}
	return ret;
}

int wm_socket_listen(int sock, int backlog) {
	int ret;

	ret = listen(sock, backlog);
	if (ret < 0) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
	}
	return ret;
}

int wm_socket_accept(int sock, struct sockaddr_in *sa, socklen_t *len) {
	int connfd;
	connfd = accept(sock, (struct sockaddr*) sa, len);
	//errno != EAGAIN  不能再读了
	if (connfd < 0 && errno != EAGAIN) {
		wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
	}
	return connfd;
}

ssize_t wm_socket_recv(int sock, void *buf, size_t len, int flag) {
	ssize_t ret;

	/**
	 * 成功返回发送的字节数；失败返回-1，同时errno被设置
	 *
	 * flags 一般设置为0，此时send为阻塞式发送
	 * 即发送不成功会一直阻塞，直到被某个信号终端终止，或者直到发送成功为止。
	 * 指定MSG_NOSIGNAL，表示当连接被关闭时不会产生SIGPIPE信号
	 * 指定MSG_DONTWAIT 表示非阻塞发送
	 * 指定MSG_OOB 表示带外数据
	 */
	ret = recv(sock, buf, len, flag);
	if (ret < 0 && errno != EAGAIN) {
		//wmWarn("Error has occurred: (errno %d) %s", errno, strerror(errno));
	}
	return ret;
}

ssize_t wm_socket_send(int sock, const void *buf, size_t len, int flag) {
	ssize_t ret;

	ret = send(sock, buf, len, flag);
	if (ret < 0 && errno != EAGAIN) {
		//wmWarn("Error has occurred: (fd=%d,errno %d) %s", sock, errno, strerror(errno));
	}
	return ret;
}

int wm_socket_close(int fd) {
	int ret;
	ret = close(fd);
	if (ret < 0) {
		//wmWarn("Error has occurred: (fd=%d,errno %d) %s", fd, errno, strerror(errno));
	}
	return ret;
}

int wm_socket_reuse_port(int fd) {
	int reusePort = 1;
	int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reusePort, sizeof(reusePort));
	if (ret < 0) {
		wmWarn("Error has occurred: (fd=%d,errno %d) %s", fd, errno, strerror(errno));
	}
	return ret;
}
