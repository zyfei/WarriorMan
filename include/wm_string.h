#ifndef _WM_STRING_H
#define _WM_STRING_H

typedef struct {
	size_t length; //当前字符串长度
	size_t size; //为字符串申请总空间大小
	off_t offset;
	char *str;
} wmString;

wmString* wmString_new(size_t size);
void wmString_print(wmString *str);
wmString *wmString_dup2(wmString *src);
wmString *wmString_dup(const char *src_str, size_t length);
int wmString_append(wmString *str, wmString *append_str);
int wmString_append_int(wmString *str, int value);
int wmString_append_ptr(wmString *str, const char *append_str, size_t length);
int wmString_write(wmString *str, size_t offset, wmString *write_str);
int wmString_write_ptr(wmString *str, off_t offset, char *write_str, size_t length);
int wmString_extend(wmString *str, size_t new_size);
char* wmString_alloc(wmString *str, size_t __size);
u_int32_t wmString_utf8_decode(char **p, size_t n);
size_t wmString_utf8_length(char *p, size_t n);
void wmString_random_string(char *buf, size_t size);
void wmString_free(wmString *str);
void wmString_replace(wmString *str, char *find, char *replace); //字符串替换

#endif    /* _WM_STRING_H */
