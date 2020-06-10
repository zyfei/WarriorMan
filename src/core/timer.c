#include "timer.h"

#define FIRST_INDEX(v) ((v) & TVR_MASK)
#define NTH_INDEX(v, n) (((v) >> (TVR_BITS + (n) * TVN_BITS)) & TVN_MASK)

/**
 * 初始化时间轮，interval为每帧的间隔，currtime为当前时间
 */
void wmTimerWheel_init(wmTimerWheel *tw, uint16_t interval, uint64_t currtime) {
	//初始化
	memset(tw, 0, sizeof(wmTimerWheel));
	//每个时间点的毫秒间隔
	tw->interval = interval;
	//上一次的时间毫秒
	tw->lasttime = currtime;
	tw->num = 0;
	int i, j;
	//初始化第一轮的每个节点的双向链表
	//TVR_SIZE 将0000 0001 左移8位，1 0000 0000 = 2的(8)次幂 = 256
	for (i = 0; i < TVR_SIZE; ++i) {
		//指向一个地址,第一轮有256个刻度
		wmList_init(tw->tvroot.vec + i);
	}
	//初始化后面的
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < TVN_SIZE; ++j)
			wmList_init(tw->tv[i].vec + j);
	}

	//初始化备胎区
	wmList_init(&tw->so_long_node);
}

/**
 * 初始化时间结点：cb为回调，ud为用户数据
 */
void wmTimerWheel_node_init(wmTimerWheel_Node *node, timer_cb_t cb, void *ud) {
	node->next = 0;
	node->prev = 0;
	node->userdata = ud;
	//定时器回调函数
	node->callback = cb;
	//到期时间
	node->expire = 0;
}

//将定时任务节点，添加到时间轮中
static void _wmTimerWheel_add(wmTimerWheel *tw, wmTimerWheel_Node *node) {
	uint32_t expire = node->expire;	//过期时间
	uint32_t idx = expire - tw->currtick; //还有多少滴答超时
	wmListNode *head = NULL; //链表头
	//idx < 256
	if (idx < TVR_SIZE) {
		//获取这个轮盘中，相应的刻度链表头 ,第一个轮盘共有TVR_SIZE个刻度  从+0开始
		//((expire) & (256 - 1)) = expire & 1111 1111   这就是一个除256。然后取余的运算, 前提是256必须是2的幂
		//理论就是expire超过8位以后的值，肯定是可以被256整除的。然后取1111 1111的& 取与，就相当于把剩下的保存下来了 .
		//！！！ 这个取余个人认为完全多余。能满足条件说明就在这一轮了啊
		head = tw->tvroot.vec + FIRST_INDEX(expire);
	} else {		//第一个表盘无法满足需求
		int i;
		uint64_t sz;
		//循环另外4个大表盘
		for (i = 0; i < 4; ++i) {
			//ull是无符号长长整形的意思
			//TVR_BITS是第一轮的2的幂数，也就是基数。然后呢 (i + 1) * TVN_BITS) 就是第i+1个盘的基数上限. 把1向左移动这么多，就是第二个表盘的取值范围
			//因为第二个表盘，每一个格子，都是第一个表盘的总数。注意不是第二个表盘有6个格子，是2的6次幂个格子哦
			sz = (1ULL << (TVR_BITS + (i + 1) * TVN_BITS));
			if (idx < sz) {
				//(expire >> (8 + i * 6)) & ((1 << 6) - 1)
				//前面这部分是首先向右移动(8+i*6)位，也就是先把零头抹去，这个零头以后放入上一个表盘中。剩下的每一个数，代表一个上级表盘。也就是个整除的过程
				//然后取与，就是一个取余的过程
				//!!!这个取余个人认为完全多余。能满足条件说明就在这一轮了啊
				idx = NTH_INDEX(expire, i);
				head = tw->tv[i].vec + idx;
				break;
			}
		}
	}
	if (head != NULL) {
		//最后把这个节点，加入到对应表盘的对应刻度中。因为是双向循环链表，其实是加在head的上面
		wmList_add_back(head, (wmListNode*) node);
	} else { //下面是超长节点
		wmList_add_back(&tw->so_long_node, (wmListNode*) node);
	}

}

//将定时任务添加到时间轮中
void wmTimerWheel_add(wmTimerWheel *tw, wmTimerWheel_Node *node, uint32_t ticks) {
	//设置过期时间,当前执行的滴答+要执行的滴答
	//如果马上执行，就放入下一个滴答
	node->expire = tw->currtick + ((ticks > 0) ? ticks : 1);
	_wmTimerWheel_add(tw, node);
	tw->num++;
}

//快速的添加
void wmTimerWheel_add_quick(wmTimerWheel *tw, timer_cb_t cb, void *ud, uint32_t ticks) {
	wmTimerWheel_Node *node1 = (wmTimerWheel_Node *) wm_malloc(sizeof(wmTimerWheel_Node));
	bzero(node1, sizeof(wmTimerWheel_Node));
	wmTimerWheel_node_init(node1, cb, ud);
	wmTimerWheel_add(tw, node1, ticks);
}

// 删除结点
//int wmTimerWheel_del(wmTimerWheel *tw, wmTimerWheel_Node *node) {
//	if (!wmList_is_empty((wmListNode*) node)) {
//		wmList_remote((wmListNode*) node);
//		return 1;
//	}
//	return 0;
//}

/**
 * 其余表盘开始走
 * idx是对应表盘的刻度
 */
//tvnum_t *tv, int idx
//tv->vec + idx
void _timerwheel_cascade(wmTimerWheel *tw, wmListNode *head1) {
	wmListNode head;
	wmList_init(&head);
	//取出idx刻度，下面的这一条链表。需要将这一条链表打散，放入上一个表盘中
	wmList_splice(head1, &head);
	while (!wmList_is_empty(&head)) {
		//取出对应的节点
		wmTimerWheel_Node *node = (wmTimerWheel_Node*) head.next;
		wmList_remote(head.next);

		//重新添加到时间轮中
		_wmTimerWheel_add(tw, node);
	}
}

/**
 * 终于开始滴答了
 */
void _wmTimerWheelick(wmTimerWheel *tw) {
	//总滴答次数+1
	++tw->currtick;

	//printf("tick=> %d \n", tw->currtick);

	//获取当前滴答数
	uint32_t currtick = tw->currtick;
	//这是判断滴答到哪里了  也就是个取余操作了。
	int index = (currtick & TVR_MASK);
	//如果是正好一圈完事,秒针走到24点了，分针应该走一格了
	if (index == 0) {
		int i = 0;
		int idx;
		//开始转动其他的表盘
		do {
			//上面有解释，整除  然后取余(取余就是多余) idx就是第i+1个表盘的刻度节点
			idx = NTH_INDEX(tw->currtick, i);
			//注意下面有个条件idx==0，判断是不是当前表盘正好转完。
			_timerwheel_cascade(tw, tw->tv[i].vec + idx);

			//第三个表盘正好转完,so_long重新开始插入
			if (i == 3) {
				_timerwheel_cascade(tw, &tw->so_long_node);
			}
		} while (idx == 0 && ++i < 4);
	}

	//双向链表
	wmListNode head;
	//初始化一个双向链表
	wmList_init(&head);
	//将第一个表盘，对应滴答的节点取出来，放入head中。 并且初始化tw->tvroot.vec + index
	wmList_splice(tw->tvroot.vec + index, &head);
	//现在tw->tvroot.vec + index 指向的就是一个空节点。然后head是以前那个双向链表

	//循环
	while (!wmList_is_empty(&head)) {
		//拿出先加入的节点
		wmTimerWheel_Node *node = (wmTimerWheel_Node*) head.next;
		//拿出这个节点
		wmList_remote(head.next);

		tw->num--;
		if (node->callback) {
			//执行回调
			node->callback(node->userdata);
			//释放申请的节点
			wm_free(node);
		}
	}
}

// 更新时间轮
void wmTimerWheel_update(wmTimerWheel *tw, uint64_t currtime) {
	//如果当前时间，大于定时器最后时间
	if (currtime > tw->lasttime) {
		//当前时间 - 定时器上次最后时间 + 上次剩余的毫秒
		int diff = currtime - tw->lasttime + tw->remainder;
		//每个时间点的毫秒间隔,初始化的时候传入。我默认1毫秒了
		int intv = tw->interval;
		//lasttime设置为这次传入的时间戳
		tw->lasttime = currtime;
		//循环滴答，滴答滴答滴答 哈哈哈
		while (diff >= intv) {
			diff -= intv;
			_wmTimerWheelick(tw);
		}
		//剩余毫秒保存起来
		tw->remainder = diff;
	}
}

/**
 * 清空定时器
 */
void wmTimerWheel_clear(wmTimerWheel *tw) {
	int i, j;
	wmListNode head;
	//清空第一个轮
	//TVR_SIZE 将0000 0001 左移8位，1 0000 0000 = 2的(8)次幂 = 256
	for (i = 0; i < TVR_SIZE; ++i) {
		//双向链表
		wmList_init(&head);
		wmList_splice(tw->tvroot.vec + i, &head);
		//循环清空
		while (!wmList_is_empty(&head)) {
			//拿出先加入的节点
			wmTimerWheel_Node *node = (wmTimerWheel_Node*) head.next;
			//拿出这个节点
			wmList_remote(head.next);
			wm_free(node);
		}
	}
	//清空后面几个轮
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < TVN_SIZE; ++j) {
			//双向链表
			wmList_init(&head);
			wmList_splice(tw->tvroot.vec + j, &head);
			//循环清空
			while (!wmList_is_empty(&head)) {
				//拿出先加入的节点
				wmTimerWheel_Node *node = (wmTimerWheel_Node*) head.next;
				//拿出这个节点
				wmList_remote(head.next);
				wm_free(node);
			}
		}
	}

	//清空备胎区
	wmList_init(&head);
	wmList_splice(&tw->so_long_node, &head);
	//循环清空
	while (!wmList_is_empty(&head)) {
		//拿出先加入的节点
		wmTimerWheel_Node *node = (wmTimerWheel_Node*) head.next;
		//拿出这个节点
		wmList_remote(head.next);
		wm_free(node);
	}
	//元素设置为0
	tw->num = 0;
}
