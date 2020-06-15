#include "wm_string.h"

/**
 * new一个string
 */
wmString* wmString_new(size_t size) {
	wmString *str = (wmString*) wm_malloc(sizeof(wmString));
	if (str == NULL) {
		wmWarn("wm_malloc[1] failed");
		return NULL;
	}

	str->length = 0;
	str->size = size;
	str->offset = 0;
	str->str = (char*) wm_malloc(size + 1); //多加一个字节，给\0用
	if (str->str == NULL) {
		wmWarn("wm_malloc[2](%ld) failed", size);
		wm_free(str);
		return NULL;
	}
	str->str[0] = '\0';
	return str;
}

/**
 * 打印string
 */
void wmString_print(wmString *str) {
	php_printf("String[length=%zu,size=%zu,offset=%jd]=%.*s\n", str->length, str->size, (intmax_t) str->offset, (int) str->length, str->str);
}

/**
 * 完全复制一个字符串
 */
wmString *wmString_dup2(wmString *src) {
	wmString *dst = wmString_new(src->size);
	if (dst) {
		//wmTrace("string dup2.  new=%p, old=%p\n", dst, src);
		dst->length = src->length;
		dst->offset = src->offset;
		memcpy(dst->str, src->str, src->length + 1); //把\0也复制过来
	}

	return dst;
}

/**
 * 创建
 */
wmString *wmString_dup(const char *src_str, size_t length) {
	//创建一个新的
	wmString *str = wmString_new(length);
	if (str) {
		//字符串长度
		str->length = length;
		memcpy(str->str, src_str, length);
		str->str[length] = '\0';
	}
	return str;
}

/**
 * 追加字符串
 */
int wmString_append(wmString *str, wmString *append_str) {
	size_t new_size = str->length + append_str->length;
	//如果字符串不够了，动态调整大小
	if (new_size > str->size) {
		//动态调整内存,调整为两倍于新内存
		if (wmString_extend(str, new_size * 2) == false) {
			return false;
		}
	}
	//追加字符串
	memcpy(str->str + str->length, append_str->str, append_str->length);
	str->length += append_str->length;
	str->str[str->length] = '\0'; //追加\0
	return true;
}

/**
 * 追加int类型
 */
int wmString_append_int(wmString *str, int value) {
	char buf[16];
	//int转换成字符串
	int s_len = wm_itoa(buf, value);

	size_t new_size = str->length + s_len;
	if (new_size > str->size) {
		if (wmString_extend(str, new_size * 2) == false) {
			return false;
		}
	}

	memcpy(str->str + str->length, buf, s_len);
	str->length += s_len;

	str->str[str->length] = '\0'; //追加\0
	return true;
}

/**
 * 粗暴的添加
 */
int wmString_append_ptr(wmString *str, const char *append_str, size_t length) {
	size_t new_size = str->length + length;
	if (new_size > str->size) {
		if (wmString_extend(str, new_size * 2) == false) {
			return false;
		}
	}
	memcpy(str->str + str->length, append_str, length);
	str->length += length;

	str->str[str->length] = '\0'; //追加\0
	return true;
}

/**
 * 从第offset位开始往里写入
 */
int wmString_write(wmString *str, size_t offset, wmString *write_str) {
	size_t new_length = offset + write_str->length;
	if (new_length > str->size) {
		if (wmString_extend(str, new_length * 2) == false) {
			return false;
		}
	}
	memcpy(str->str + offset, write_str->str, write_str->length);
	if (new_length > str->length) {
		str->length = new_length;
		str->str[str->length] = '\0'; //追加\0
	}
	return true;
}

/**
 * 简单粗暴的往里写入
 */
int wmString_write_ptr(wmString *str, off_t offset, char *write_str, size_t length) {
	size_t new_length = offset + length;
	if (new_length > str->size) {
		if (wmString_extend(str, new_length * 2) == false) {
			return false;
		}
	}
	memcpy(str->str + offset, write_str, length);
	if (new_length > str->length) {
		str->length = new_length;
		str->str[str->length] = '\0'; //追加\0
	}
	return true;
}

/**
 * size不够的时候，重构字符串
 */
int wmString_extend(wmString *str, size_t new_size) {
	assert(new_size > str->size);
	//动态调整内存
	char *new_str = (char*) wm_realloc(str->str, new_size + 1);
	if (new_str == NULL) {
		wmWarn("realloc(%ld) failed", new_size);
		return false;
	}
	str->str = new_str;
	str->size = new_size;
	return true;
}

/**
 * 从这个字符串中，申请size大小的空间另做他用
 */
char* wmString_alloc(wmString *str, size_t __size) {
	//如果加完之后的长度，大于实际内存
	if (str->length + __size > str->size) {
		size_t new_length = str->size + __size;
		if (wmString_extend(str, new_length) == false) {
			return NULL;
		}
	}

	char *tmp = str->str + str->length;
	str->length += __size;
	return tmp;
}

void wmString_free(wmString *str) {
	if (str) {
		wm_free(str->str);
		wm_free(str);
	}
}

u_int32_t wmString_utf8_decode(char **p, size_t n) {
	size_t len;
	u_int32_t u, i, valid;

	u = **p;

	if (u >= 0xf0) {
		u &= 0x07;
		valid = 0xffff;
		len = 3;
	} else if (u >= 0xe0) {
		u &= 0x0f;
		valid = 0x7ff;
		len = 2;
	} else if (u >= 0xc2) {
		u &= 0x1f;
		valid = 0x7f;
		len = 1;
	} else {
		(*p)++;
		return 0xffffffff;
	}

	if (n - 1 < len) {
		return 0xfffffffe;
	}

	(*p)++;

	while (len) {
		i = *(*p)++;
		if (i < 0x80) {
			return 0xffffffff;
		}
		u = (u << 6) | (i & 0x3f);
		len--;
	}

	if (u > valid) {
		return u;
	}

	return 0xffffffff;
}

size_t wmString_utf8_length(char *p, size_t n) {
	char c, *last;
	size_t len;

	last = p + n;

	for (len = 0; p < last; len++) {
		c = *p;
		if (c < 0x80) {
			p++;
			continue;
		}
		if (wmString_utf8_decode(&p, n) > 0x10ffff) {
			/* invalid UTF-8 */
			return n;
		}
	}
	return len;
}

void wmString_random_string(char *buf, size_t size) {
	static char characters[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
		'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2',
		'3', '4', '5', '6', '7', '8', '9', };
	unsigned int i;
	for (i = 0; i < size; i++) {
		buf[i] = characters[wm_rand(0, sizeof(characters) - 1)];
	}
	buf[i] = '\0';
}

void wmString_replace(wmString *str, char *find, char *replace) {
	//新建一个新字符串，存储替换后的str
	wmString *dst = wmString_dup2(str);

	str->length = 0;
	for (int i = 0; i < dst->length; i++) {
		//如果找到了
		if (!strncmp(dst->str + i, find, strlen(find))) { //查找目标字符串
			wmString_append_ptr(str, replace, strlen(replace)); //保存一字节进缓冲区
			i += strlen(find) - 1;
		} else {
			wmString_append_ptr(str, dst->str + i, 1); //保存一字节进缓冲区
		}
	}

	wmString_free(dst);
}
