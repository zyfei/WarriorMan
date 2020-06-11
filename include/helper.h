/**
 * 一些基础的公共方法
 */
#ifndef WM_HELPER_H
#define WM_HELPER_H

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

//获取当前时间
static inline void wmGetTime(long *seconds, long *microseconds) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*seconds = tv.tv_sec;
	*microseconds = tv.tv_usec;
}

//只获取毫秒
static inline void wmGetMilliTime(long *microseconds) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*microseconds = tv.tv_sec * 1000 + (long) tv.tv_usec / 1000;
}

//只获取微妙
static inline void wmGetMicroTime(long *microseconds) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*microseconds = tv.tv_usec;
}

static inline uint64_t touint64(int fd, int id) {
	uint64_t ret = 0;
	ret |= ((uint64_t) fd) << 32;
	ret |= ((uint64_t) id);

	return ret;
}

static inline void fromuint64(uint64_t v, int *fd, int *id) {
	*fd = (int) (v >> 32);
	*id = (int) (v & 0xffffffff);
}

/**
 * 格式化字符串
 */
static inline size_t wm_snprintf(char *buf, size_t size, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int retval = vsnprintf(buf, size, format, args);
	va_end(args);
	if (retval < 0) {
		retval = 0;
		buf[0] = '\0';
	} else if (retval >= size) {
		retval = size - 1;
		buf[retval] = '\0';
	}
	return retval;
}

#endif	/* WM_HELPER_H */
