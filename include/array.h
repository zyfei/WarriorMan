#ifndef WM_ARRAY_H
#define WM_ARRAY_H

#include "header.h"
#include "log.h"

/**
 * The default wmArray->pages pointer array is WM_ARRAY_PAGE_MAX,
 * it means you can manage up to (WM_ARRAY_PAGE_MAX*page_size) elements
 */
#define WM_ARRAY_PAGE_MAX      1024

typedef struct _wmArray {
	void **pages;

	/**
	 * number of page
	 */
	uint16_t page_num;

	/**
	 * 每页的数量多少
	 */
	uint16_t page_size;

	/**
	 * 每个元素的大小
	 */
	uint32_t item_size;

	/**
	 * number of data
	 */
	uint32_t item_num;
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
