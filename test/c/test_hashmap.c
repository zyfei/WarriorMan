#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include "../../src/core/hashmap.c"
#include "../include/log.h"

int main() {
	swHashMap *hm = swHashMap_new(NULL);

	swHashMap_add(hm, (char *) SW_STRL("hello"), (void *) 199);
	swHashMap_add(hm, (char *) SW_STRL("swoole22"), (void *) 8877);
	swHashMap_add(hm, (char *) SW_STRL("hello2"), (void *) 200);
	swHashMap_add(hm, (char *) SW_STRL("willdel"), (void *) 888);
	swHashMap_add(hm, (char *) SW_STRL("willupadte"), (void *) 9999);
	swHashMap_add(hm, (char *) SW_STRL("hello3"), (void *) 78978);

	int ret1 = (int) (long) swHashMap_find(hm, (char *) SW_STRL("willdel"));
	printf("%d \n",ret1);

	swHashMap_del(hm, (char *) SW_STRL("willdel"));
	swHashMap_update(hm, (char *) SW_STRL("willupadte"),
			(void *) (9999 * 5555));

	ret1 = (int) (long) swHashMap_find(hm, (char *) SW_STRL("hello"));

	int ret2 = (int) (long) swHashMap_find(hm, (char *) SW_STRL("hello2"));

	int ret3 = (int) (long) swHashMap_find(hm, (char *) SW_STRL("notfound"));

	char *key;
	int data;

	while (1) {
		data = (int) (long) swHashMap_each(hm, &key);
		if (!data) {
			break;
		}
	}
	swHashMap_free(hm);
}
