#include <stdio.h>
#include <string.h>
#include "../include/khash.h"

KHASH_MAP_INIT_INT(test_map, int);

int main(int argc, char *argv[]) {

	//访问键、值都需要使用 iter
	khiter_t iter;
	int ret;

	//初始化test_map类型的mao
	khash_t(test_map) *h = kh_init(test_map);

	//放入成功后返回值 iter 即表示为 100 这个键在 map 中的位置
	iter = kh_put(test_map, h, 1, &ret);

	//kh_key(h, iter) = strdup("abc");
	kh_value(h,iter) = 101;

	iter = kh_get(test_map, h, 1);
	printf("%d\n", kh_value(h, iter));

	for (int k = kh_begin(h); k != kh_end(h); k++) {
		if (kh_exist(h, iter)) {
//			free((int*) kh_key(h, iter));
//			free((int*) kh_value(h, iter));
		}
	}

	kh_destroy(test_map, h);

}
