<?php
Workerman\Runtime::enableCoroutine();

worker_go(function () {
	var_dump(Workerman::getCid());
	sleep(3);
	worker_go(function () {
		var_dump(Workerman::getCid());
	});
	var_dump(Workerman::getCid());
});

worker_go(function () {
	var_dump(Workerman::getCid());
});

worker_event_wait();