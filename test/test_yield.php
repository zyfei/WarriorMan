<?php
Corkerman\Runtime::enableCoroutine();

$arr[] = worker_go(function () {
	var_dump(Corkerman::getCid() . " start");
	Corkerman::yield();
	var_dump(Corkerman::getCid() . " end");
});

$arr[] = worker_go(function () {
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