/**
 * channel入口文件
 */
#include "base.h"
#include "channel.h"

zend_class_entry workerman_channel_ce;
zend_class_entry *workerman_channel_ce_ptr;

//为了通过php对象，找到上面的c对象
typedef struct {
	wmChannel *chan; //c对象
	zend_object std; //php对象
} wmChannelObject;

//zend_object_handlers实际上就是我们在PHP脚本上面操作一个PHP对象的时候，底层会去调用的函数。
static zend_object_handlers workerman_channel_handlers;

/**
 * 通过这个PHP对象找到我们的wmChannelObject对象的代码
 */
static wmChannelObject* wmChannel_fetch_object(zend_object *obj) {
	return (wmChannelObject*) ((char*) obj - workerman_channel_handlers.offset);
}

/**
 * 创建一个php对象
 * zend_class_entry是一个php类
 */
static zend_object* wmChannel_create_object(zend_class_entry *ce) {
	//向PHP申请一块内存,大小是一个coro_chan的大小+
	//至于为什么不根据size来直接申请内存，因为zend_object最后一个元素是一个数组下标，后面不一定申请了多少个
	//如果我们直接分配zend_object的大小，就会把PHP对象的属性给漏掉。这是实现自定义对象需要特别关注的问题。如果还是不理解，小伙伴们可以先去学习C语言的柔性数组。
	wmChannelObject *chan_t = (wmChannelObject*) ecalloc(1, sizeof(wmChannelObject) + zend_object_properties_size(ce));
	//std之前的一个指针位置，就是我们的wmChannel指针

	//初始化php对象，根据ce
	zend_object_std_init(&chan_t->std, ce);
	//属性初始化
	object_properties_init(&chan_t->std, ce);
	//handler的初始化
	chan_t->std.handlers = &workerman_channel_handlers;
	return &chan_t->std;
}

/**
 * 释放php对象的方法
 */
static void wmChannel_free_object(zend_object *object) {
	wmChannelObject *chan_t = (wmChannelObject*) wmChannel_fetch_object(object);
	wmChannel *chan = chan_t->chan;
	if (chan) {
		wmChannel_free(chan);
	}
	//销毁zend_object，析构函数
	zend_object_std_dtor(&chan_t->std);
}

//构造函数
ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_channel_construct, 0, 0, 0) //
ZEND_ARG_INFO(0, capacity)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_channel_push, 0, 0, 1)	//
ZEND_ARG_INFO(0, data)
ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_channel_pop, 0, 0, 0) //
ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_workerman_channel_void, 0, 0, 0) //
ZEND_END_ARG_INFO()

//构造函数
PHP_METHOD(workerman_channel, __construct) {
	wmChannelObject *chan_t;
	zend_long capacity = 1;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
				Z_PARAM_OPTIONAL
				Z_PARAM_LONG(capacity)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	if (capacity <= 0) {
		capacity = 1;
	}

	chan_t = (wmChannelObject*) wmChannel_fetch_object(Z_OBJ_P(getThis()));
	chan_t->chan = wmChannel_create(capacity);

	zend_update_property_long(workerman_channel_ce_ptr, getThis(), ZEND_STRL("capacity"), capacity);
}

static PHP_METHOD(workerman_channel, push) {
	wmChannelObject *chan_t;
	wmChannel *chan;
	zval *zdata;
	double timeout = -1;

	ZEND_PARSE_PARAMETERS_START(1, 2)
				Z_PARAM_ZVAL(zdata)
				Z_PARAM_OPTIONAL
				Z_PARAM_DOUBLE(timeout)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	chan_t = (wmChannelObject*) wmChannel_fetch_object(Z_OBJ_P(getThis()));
	chan = chan_t->chan;

	//引用计数+1
	Z_TRY_ADDREF_P(zdata);
	//这里需要拷贝出一份zdata才可以，因为当协程销毁的时候，这个zdata也销毁了
	zdata = wm_zval_dup(zdata);

	if (!wmChannel_push(chan, zdata, timeout)) {
		Z_TRY_DELREF_P(zdata);
		efree(zdata);
		RETURN_FALSE
	}
	RETURN_TRUE
}

static PHP_METHOD(workerman_channel, pop) {
	wmChannelObject *chan_t;
	wmChannel *chan;
	double timeout = -1;

	ZEND_PARSE_PARAMETERS_START(0, 1)
				Z_PARAM_OPTIONAL
				Z_PARAM_DOUBLE(timeout)
			ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	chan_t = (wmChannelObject*) wmChannel_fetch_object(Z_OBJ_P(getThis()));
	chan = chan_t->chan;

	zval *zdata = (zval*) wmChannel_pop(chan, timeout);
	if (!zdata) {
		RETURN_FALSE
	}
	RETVAL_ZVAL(zdata, 0, 0);
	efree(zdata);
}

/**
 * 通道是否为空
 */
PHP_METHOD(workerman_channel, isEmpty) {
	wmChannelObject *chan_t;
	wmChannel *chan;
	chan_t = (wmChannelObject*) wmChannel_fetch_object(Z_OBJ_P(getThis()));
	chan = chan_t->chan;
	int num = wmChannel_num(chan);
	bool is_exist = (num > 0);
	RETURN_BOOL(is_exist);
}

/**
 * 当前通道剩余数量
 */
PHP_METHOD(workerman_channel, length) {
	wmChannelObject *chan_t;
	wmChannel *chan;
	chan_t = (wmChannelObject*) wmChannel_fetch_object(Z_OBJ_P(getThis()));
	chan = chan_t->chan;
	RETURN_LONG(wmChannel_num(chan));
}

static const zend_function_entry workerman_channel_methods[] = { //
	PHP_ME(workerman_channel, __construct, arginfo_workerman_channel_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR) //
		PHP_ME(workerman_channel, push, arginfo_workerman_channel_push, ZEND_ACC_PUBLIC) //
		PHP_ME(workerman_channel, pop, arginfo_workerman_channel_pop, ZEND_ACC_PUBLIC) //
		PHP_ME(workerman_channel, isEmpty, arginfo_workerman_channel_void, ZEND_ACC_PUBLIC) //
		PHP_ME(workerman_channel, length, arginfo_workerman_channel_void, ZEND_ACC_PUBLIC) //
		PHP_FE_END };

/**
 * 注册我们的WorkerMan\Server这个类
 */
void workerman_channel_init() {
	//定义好一个类
	INIT_NS_CLASS_ENTRY(workerman_channel_ce, "Warriorman", "Channel", workerman_channel_methods);
	//在zedn中注册类
	workerman_channel_ce_ptr = zend_register_internal_class(&workerman_channel_ce TSRMLS_CC); // 在 Zend Engine 中注册

	//替换掉PHP默认的handler
	memcpy(&workerman_channel_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	//php对象实例化已经由我们自己的代码接管了
//	ST_SET_CLASS_CUSTOM_OBJECT(workerman_channel,
//			wmChannel_create_object,wmChannel_free_object,
//			wmChannelObject, std);
	//上面的宏翻译一下，就是下面的
	workerman_channel_ce_ptr->create_object = wmChannel_create_object;
	workerman_channel_handlers.free_obj = wmChannel_free_object;
	workerman_channel_handlers.offset = (zend_long) (((char*) (&(((wmChannelObject*) NULL)->std))) - ((char*) NULL));

	//类进行初始化的时候设置变量
	zend_declare_property_long(workerman_channel_ce_ptr, ZEND_STRL("capacity"), 1, ZEND_ACC_PUBLIC);

}
