<?php
/**
 * hook了系统函数
 * 目前支持
 * 	sleep
 */
Warriorman\Runtime::enableCoroutine();

Warriorman::create(function () {
	var_dump(Warriorman::getCid());
	sleep(1);
	worker_go(function () {
		var_dump(Warriorman::getCid());
	});
	var_dump(Warriorman::getCid());
});

Warriorman::create(function () {
	sleep(1);
	var_dump(Warriorman::getCid());
});

if (! defined("RUN_TEST")) {
	worker_event_wait();
}