/**
 * 对khash进行简单的封装
 */
#ifndef WM_HASH_H_
#define WM_HASH_H_

#include "header.h"
#include "khash.h"

#ifdef kcalloc
#undef kcalloc
#undef kmalloc
#undef krealloc
#undef kfree
#endif

#define kcalloc(N,Z) wm_calloc(N,Z)
#define kmalloc(Z) wm_malloc(Z)
#define krealloc(P,Z) wm_realloc(P,Z)
#define kfree(P) wm_free(P)

KHASH_MAP_INIT_INT(WM_HASH_INT_INT, int); //键值都是int的hashmap
KHASH_MAP_INIT_INT(WM_HASH_INT_STR, void*); //键是int，值是void类型指针

#define wmHash_INT_PTR khash_t(WM_HASH_INT_STR)
#define wmHashKey khiter_t
#define wmHash_init kh_init
#define wmHash_put kh_put
#define wmHash_get kh_get
#define wmHash_del kh_del
#define wmHash_exist kh_exist
#define wmHash_value kh_value
#define wmHash_begin kh_begin
#define wmHash_end kh_end

#endif

