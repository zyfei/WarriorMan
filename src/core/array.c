#include "array.h"

/**
 *  创建数组
 */
wmArray *wmArray_new(int page_size, size_t item_size) {
	wmArray *array = wm_malloc(sizeof(wmArray));
	if (array == NULL) {
		wmWarn("malloc[0] failed");
		return NULL;
	}
	bzero(array, sizeof(wmArray));

	//一个数组最多有1024个页
	array->pages = wm_malloc(sizeof(void*) * WM_ARRAY_PAGE_MAX);
	if (array->pages == NULL) {
		wm_free(array);
		wmWarn("malloc[1] failed");
		return NULL;
	}

	array->item_size = item_size; //每个元素的大小
	array->page_size = page_size; //每页的大小

	//先为第一页申请内存
	wmArray_extend(array);

	return array;
}

/**
 * Destroy the array
 */
void wmArray_free(wmArray *array) {
	int i;
	for (i = 0; i < array->page_num; i++) {
		wm_free(array->pages[i]);
	}
	wm_free(array->pages);
	wm_free(array);
}

/**
 * 扩展数组
 */
int wmArray_extend(wmArray *array) {
	//已经达到最大页数，不给申请内存了
	if (array->page_num == WM_ARRAY_PAGE_MAX) {
		wmWarn("max page_num is %d", array->page_num);
		return WM_ERR;
	}
	//给第array->page_num页申请内存
	array->pages[array->page_num] = wm_calloc(array->page_size, array->item_size);
	if (array->pages[array->page_num] == NULL) {
		wmWarn("malloc[1] failed");
		return WM_ERR;
	}
	//页数增加
	array->page_num++;
	return WM_OK;
}

/**
 * 通过下标查找元素
 */
void *wmArray_find(wmArray *array, uint32_t n) {
	//计算对应的页数
	int page = wmArray_page(array, n);
	if (page >= array->page_num) {
		return NULL;
	}
	//对应页的指针+位移量
	return (char*) array->pages[page] + (wmArray_offset(array, n) * array->item_size);
}

/**
 * 添加元素，返回下标
 */
int wmArray_add(wmArray *array, void *data) {
	int n = array->offset++; //下标
	int page = wmArray_page(array, n); //对应页数
	//如果是新页，申请内存
	if (page >= array->page_num && wmArray_extend(array) < 0) {
		return WM_ERR;
	}
	//元素数量
	array->item_num++;
	memcpy((char*) array->pages[page] + (wmArray_offset(array, n) * array->item_size), data, array->item_size);
	return n;
}

/**
 * 替换
 */
int wmArray_set(wmArray *array, uint32_t n, void *data) {
	int page = wmArray_page(array, n);
	if (page >= array->page_num) {
		wmWarn("find index[%d] out of array", n);
		return WM_ERR;
	}
	memcpy((char*) array->pages[page] + (wmArray_offset(array, n) * array->item_size), data, array->item_size);
	return WM_OK;
}

void wmArray_printf(wmArray *array) {
	for (int i = 0; i < array->offset; i++) {
		int *pid = wmArray_find(array, i);
		printf("key=%d value=%d \n", i, *pid);
	}
}

/**
 * 没找到就申请
 */
void *wmArray_alloc(wmArray *array, uint32_t n) {
	while (n >= array->page_num * array->page_size) {
		if (wmArray_extend(array) < 0) {
			return NULL;
		}
	}

	int page = wmArray_page(array, n);
	if (page >= array->page_num) {
		wmWarn("find index[%d] out of array", n);
		return NULL;
	}
	return (char*) array->pages[page] + (wmArray_offset(array, n) * array->item_size);
}

//重新开始
void wmArray_clear(wmArray *array) {
	array->offset = 0;
	array->item_num = 0;
}
