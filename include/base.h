#ifndef _WM_BASH_H
#define _WM_BASH_H

/**
 * 引入一些自己做的基础库
 */
#include "header.h"
#include "timer.h"
#include "socket.h"
#include "stack.h"
#include "log.h"
#include "queue.h"
#include "wm_string.h"
#include "file.h"
#include "array.h"
#include "hash.h"


//构造函数用到
typedef struct {
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} php_fci_fcc;

/** 兼容性设置 **/

#if PHP_VERSION_ID < 70100
#error "require PHP version 7.1 or later"
#endif

// Fixed in php-7.0.28, php-7.1.15RC1, php-7.2.3RC1 (https://github.com/php/php-src/commit/e88e83d3e5c33fcd76f08b23e1a2e4e8dc98ce41)
#if PHP_MAJOR_VERSION == 7 && ((PHP_MINOR_VERSION == 0 && PHP_RELEASE_VERSION < 28) || (PHP_MINOR_VERSION == 1 && PHP_RELEASE_VERSION < 15) || (PHP_MINOR_VERSION == 2 && PHP_RELEASE_VERSION < 3))
// See https://github.com/php/php-src/commit/0495bf5650995cd8f18d6a9909eb4c5dcefde669
// Then https://github.com/php/php-src/commit/2dcfd8d16f5fa69582015cbd882aff833075a34c
#if PHP_VERSION_ID < 70100
#define zend_wrong_parameters_count_error zend_wrong_paramers_count_error
#endif

// See https://github.com/php/php-src/commit/52db03b3e52bfc886896925d050af79bc4dc1ba3
#if PHP_MINOR_VERSION == 2
#define WM_ZEND_WRONG_PARAMETERS_COUNT_ERROR zend_wrong_parameters_count_error(_flags & ZEND_PARSE_PARAMS_THROW, _num_args, _min_num_args, _max_num_args)
#else
#define WM_ZEND_WRONG_PARAMETERS_COUNT_ERROR zend_wrong_parameters_count_error(_num_args, _min_num_args, _max_num_args)
#endif

#undef ZEND_PARSE_PARAMETERS_START_EX

#define ZEND_PARSE_PARAMETERS_START_EX(flags, min_num_args, max_num_args) do { \
        const int _flags = (flags); \
        int _min_num_args = (min_num_args); \
        int _max_num_args = (max_num_args); \
        int _num_args = EX_NUM_ARGS(); \
        int _i; \
        zval *_real_arg, *_arg = NULL; \
        zend_expected_type _expected_type = Z_EXPECTED_LONG; \
        char *_error = NULL; \
        zend_bool _dummy; \
        zend_bool _optional = 0; \
        int error_code = ZPP_ERROR_OK; \
        ((void)_i); \
        ((void)_real_arg); \
        ((void)_arg); \
        ((void)_expected_type); \
        ((void)_error); \
        ((void)_dummy); \
        ((void)_optional); \
        \
        do { \
            if (UNEXPECTED(_num_args < _min_num_args) || \
                (UNEXPECTED(_num_args > _max_num_args) && \
                 EXPECTED(_max_num_args >= 0))) { \
                if (!(_flags & ZEND_PARSE_PARAMS_QUIET)) { \
                    WM_ZEND_WRONG_PARAMETERS_COUNT_ERROR; \
                } \
                error_code = ZPP_ERROR_FAILURE; \
                break; \
            } \
            _i = 0; \
            _real_arg = ZEND_CALL_ARG(execute_data, 0);
#endif

/* PHP 7.3 compatibility macro {{{*/
#ifndef GC_SET_REFCOUNT
# define GC_SET_REFCOUNT(p, rc) do { \
    GC_REFCOUNT(p) = rc; \
} while (0)
#endif

#ifndef GC_ADDREF
#define GC_ADDREF(ref) ++GC_REFCOUNT(ref)
#define GC_DELREF(ref) --GC_REFCOUNT(ref)
#endif

#ifndef GC_IS_RECURSIVE
#define GC_IS_RECURSIVE(p) \
    (ZEND_HASH_GET_APPLY_COUNT(p) >= 1)
#define GC_PROTECT_RECURSION(p) \
    ZEND_HASH_INC_APPLY_COUNT(p)
#define GC_UNPROTECT_RECURSION(p) \
    ZEND_HASH_DEC_APPLY_COUNT(p)
#endif

#ifndef ZEND_CLOSURE_OBJECT
#define ZEND_CLOSURE_OBJECT(func) (zend_object*)func->op_array.prototype
#endif

#ifndef ZEND_HASH_APPLY_PROTECTION
#define ZEND_HASH_APPLY_PROTECTION(p) 1
#endif/*}}}*/

/* PHP 7.4 compatibility macro {{{*/
#ifndef E_FATAL_ERRORS
#define E_FATAL_ERRORS (E_ERROR | E_CORE_ERROR | E_COMPILE_ERROR | E_USER_ERROR | E_RECOVERABLE_ERROR | E_PARSE)
#endif

#ifndef ZEND_THIS
#define ZEND_THIS (&EX(This))
#endif
/*}}}*/

/**  封装的api **/
#define php_workerman_array_length(zarray)        zend_hash_num_elements(Z_ARRVAL_P(zarray))
#define php_workerman_array_get_value(ht, str, v) ((v = zend_hash_str_find(ht, str, sizeof(str)-1)) && !ZVAL_IS_NULL(v))

#ifndef ZVAL_IS_BOOL
static inline zend_bool ZVAL_IS_BOOL(zval *v) {
	return Z_TYPE_P(v) == IS_TRUE || Z_TYPE_P(v) == IS_FALSE;
}
#endif

#ifndef Z_BVAL_P
static inline zend_bool Z_BVAL_P(zval *v) {
	return Z_TYPE_P(v) == IS_TRUE;
}
#endif

#ifndef ZVAL_IS_ARRAY
static inline zend_bool ZVAL_IS_ARRAY(zval *v) {
	return Z_TYPE_P(v) == IS_ARRAY;
}
#endif

static inline int php_workerman_array_length_safe(zval *zarray) {
	if (zarray && ZVAL_IS_ARRAY(zarray)) {
		return php_workerman_array_length(zarray);
	} else {
		return 0;
	}
}

/**
 * Socket类
 */
extern zend_class_entry workerman_socket_ce;
extern zend_class_entry *workerman_socket_ce_ptr;

//定义一些全局方法
static inline zval *wm_zend_read_property(zend_class_entry *ce, zval *obj, const char *s, int len, int silent) {
	zval rv, *property = zend_read_property(ce, obj, s, len, silent, &rv);
	if (UNEXPECTED(property == &EG(uninitialized_zval))) {
		zend_update_property_null(ce, obj, s, len);
		return zend_read_property(ce, obj, s, len, silent, &rv);
	}
	return property;
}

static inline zval* wm_zend_read_property_not_null(zend_class_entry *ce, zval *obj, const char *s, int len, int silent) {
	zval rv, *property = zend_read_property(ce, obj, s, len, silent, &rv);
	zend_uchar type = Z_TYPE_P(property);
	return (type == IS_NULL || UNEXPECTED(type == IS_UNDEF)) ? NULL : property;
}

static inline zval* wm_zend_read_static_property_not_null(zend_class_entry *ce, const char *s, int len, int silent) {
	zval *property = zend_read_static_property(ce, s, len, silent);
	zend_uchar type = Z_TYPE_P(property);
	return (type == IS_NULL || UNEXPECTED(type == IS_UNDEF)) ? NULL : property;
}

static inline zval* wm_malloc_zval() {
	return (zval *) emalloc(sizeof(zval));
}

static inline zval* wm_zval_dup(zval *val) {
	zval *dup = wm_malloc_zval();
	memcpy(dup, val, sizeof(zval));
	return dup;
}

//zend_fcall_info_cache 引用加一
static inline void wm_zend_fci_cache_persist(zend_fcall_info_cache *fci_cache) {
	if (fci_cache->object) {
		GC_ADDREF(fci_cache->object);
	}
	if (fci_cache->function_handler->op_array.fn_flags & ZEND_ACC_CLOSURE) {
		GC_ADDREF(ZEND_CLOSURE_OBJECT(fci_cache->function_handler));
	}
}

//zend_fcall_info_cache 引用减一
static inline void wm_zend_fci_cache_discard(zend_fcall_info_cache *fci_cache) {
	if (fci_cache->object) {
		OBJ_RELEASE(fci_cache->object);
	}
	if (fci_cache->function_handler->op_array.fn_flags & ZEND_ACC_CLOSURE) {
		OBJ_RELEASE(ZEND_CLOSURE_OBJECT(fci_cache->function_handler));
	}
}

/* use void* to match some C callback function pointers */
static inline void wm_zend_fci_cache_free(void* fci_cache) {
	wm_zend_fci_cache_discard((zend_fcall_info_cache *) fci_cache);
	efree((zend_fcall_info_cache * ) fci_cache);
}

//调用闭包函数
int call_closure_func(php_fci_fcc* fci_fcc);
bool set_process_title(char* process_title);

//开启协程，hook相关函数
void wm_enableCoroutine();

//初始化base相关
void workerman_base_init();
void workerman_base_shutdown();

//定义协程注册方法
void workerman_coroutine_init();
//协程类静态create方法，封装为方法
long worker_go();
//connection注册方法
void workerman_connection_init();
//worker注册方法
void workerman_worker_init();
//channel注册方法
void workerman_channel_init();
//runtime注册方法
void workerman_runtime_init();

//定义的一些结构体
typedef struct {
	int epollfd; //创建的epollfd。
	int ncap; //epoll回调可以接收最多事件数量
	int event_num; // 当前在监听的事件的数量
	struct epoll_event *events; //是用来保存epoll返回的事件。
	struct epoll_event *event; //用来储存每一次添加修改epoll的临时变量
} wmPoll_t;

typedef struct {
	bool is_running;
	wmPoll_t *poll;
	wmTimerWheel timer; //核心定时器
	wmString *buffer_stack; //用于整个项目的临时字符串存储
	wmString *buffer_stack_large; //用于整个项目的临时字符串存储,有时候一个不够用
} wmGlobal_t;

//初始化wmPoll_t
int init_wmPoll();
//释放
int free_wmPoll();

int wm_event_wait();

//定义的全局变量，在base.cc中
extern wmGlobal_t WorkerG;

#endif	/* _WM_BASH_H */
