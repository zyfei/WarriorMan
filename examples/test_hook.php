<?php
/**
 * hook了系统函数
 * 目前支持
 * 	sleep
 */
Corkerman\Runtime::enableCoroutine();

Corkerman::create(function () {
	var_dump(Corkerman::getCid());
	sleep(1);
	worker_go(function () {
		var_dump(Corkerman::getCid());
	});
	var_dump(Corkerman::getCid());
});

Corkerman::create(function () {
	sleep(1);
	var_dump(Corkerman::getCid());
});

if (! defined("RUN_TEST")) {
	worker_event_wait();
}