#ifndef WORKERMAN_HEADER_H_
#define WORKERMAN_HEADER_H_

#include "php.h"
#include "php_ini.h"
#include "php_network.h"
#include "php_streams.h"
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
#include <grp.h>

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

//php库
#include "zend_closures.h"
#include "zend_exceptions.h"
#include "zend_object_handlers.h"

//公共配置
#include "workerman_config.h"
//公共方法
#include "helper.h"


#endif /* WORKERMAN_HEADER_H_ */
