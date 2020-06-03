<?php
Corkerman\Runtime::enableCoroutine();

worker_go(function () {
	var_dump(Corkerman::getCid());
	sleep(3);
	worker_go(function () {
		var_dump(Corkerman::getCid());
	});
	var_dump(Corkerman::getCid());
});

worker_go(function () {
	var_dump(Corkerman::getCid());
});

if (! defined("RUN_TEST")) {
	worker_event_wait();
}