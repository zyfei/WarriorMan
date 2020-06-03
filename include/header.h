#ifndef WORKERMAN_HEADER_H_
#define WORKERMAN_HEADER_H_

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// include standard library
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <sys/timeb.h>
#include <stdbool.h>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/epoll.h>

//公共配置
#include "workerman_config.h"

/**
 * 把数字转换成字符串
 */
static inline int wm_itoa(char *buf, long value) {
	long i = 0, j;
	long sign_mask;
	unsigned long nn;

	sign_mask = value >> (sizeof(long) * 8 - 1);
	nn = (value + sign_mask) ^ sign_mask;
	do {
		buf[i++] = nn % 10 + '0';
	} while (nn /= 10);

	buf[i] = '-';
	i += sign_mask & 1;
	buf[i] = '\0';

	int s_len = i;
	char swap;

	for (i = 0, j = s_len - 1; i < j; ++i, --j) {
		swap = buf[i];
		buf[i] = buf[j];
		buf[j] = swap;
	}
	buf[s_len] = 0;
	return s_len;
}

/**
 * 随机
 */
static inline int wm_rand(int min, int max) {
	static int _seed = 0;
	assert(max > min);

	if (_seed == 0) {
		_seed = time(NULL);
		srand(_seed);
	}

	int _rand = rand();
	_rand = min + (int) ((double) ((double) (max) - (min) + 1.0) * ((_rand) / ((RAND_MAX) + 1.0)));
	return _rand;
}

#endif /* WORKERMAN_HEADER_H_ */
