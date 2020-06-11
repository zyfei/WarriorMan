#ifndef WM_ARRAY_H
#define WM_ARRAY_H

#include "header.h"
#include "log.h"

typedef struct _wmArray {
	void **pages;
	uint16_t page_num; //当前的页数
	uint16_t page_size; //每页的数量多少
	uint32_t item_size; //每个元素的大小
	uint32_t item_num; //有多少元素了
	uint32_t offset;
} wmArray;

#define wmArray_page(array, n)      ((n) / (array)->page_size)
#define wmArray_offset(array, n)    ((n) % (array)->page_size)

wmArray *wmArray_new(int page_size, size_t item_size);
void wmArray_free(wmArray *array);
void *wmArray_find(wmArray *array, uint32_t n);
int wmArray_set(wmArray *array, uint32_t n, void *data);
void *wmArray_alloc(wmArray *array, uint32_t n);
int wmArray_add(wmArray *array, void *data);
int wmArray_extend(wmArray *array);
void wmArray_clear(wmArray *array);
void wmArray_printf(wmArray *array);

#endif	/* WM_ARRAY_H */
