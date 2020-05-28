<?php
Workerman\Runtime::enableCoroutine();

$arr[] = worker_go(function () {
	var_dump(Workerman::getCid() . " start");
	Workerman::yield();
	var_dump(Workerman::getCid() . " end");
});

$arr[] = worker_go(function () {
	var_dump(Workerman::getCid() . " start");
	Workerman::yield();
	var_dump(Workerman::getCid() . " end");
});

foreach ($arr as $n) {
	Workerman::resume($n);
}

//worker_event_wait();