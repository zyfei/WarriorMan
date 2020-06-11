<?php
/**
 * 展示协程切换
 */
$arr[] = Corkerman::create(function () {
	var_dump(Corkerman::getCid() . " start");
	Corkerman::yield();
	var_dump(Corkerman::getCid() . " end");
});

$arr[] = Corkerman::create(function () {
	var_dump(Corkerman::getCid() . " start");
	Corkerman::yield();
	var_dump(Corkerman::getCid() . " end");
});

foreach ($arr as $n) {
	Corkerman::resume($n);
}

if (! defined("RUN_TEST")) {
	worker_event_wait();
}