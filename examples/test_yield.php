<?php
/**
 * 展示协程切换
 */
$arr[] = Warriorman::create(function () {
	var_dump(Warriorman::getCid() . " start");
	Warriorman::yield();
	var_dump(Warriorman::getCid() . " end");
});

$arr[] = Warriorman::create(function () {
	var_dump(Warriorman::getCid() . " start");
	Warriorman::yield();
	var_dump(Warriorman::getCid() . " end");
});

foreach ($arr as $n) {
	Warriorman::resume($n);
}

if (! defined("RUN_TEST")) {
	worker_event_wait();
}