<?php
/**
 * channel组件
 */
$chan = new Corkerman\Channel(1);
var_dump($chan);

Corkerman::create(function () use ($chan) {
	var_dump("push start");
	$ret = $chan->push("hello world");
	var_dump($ret);
});

Corkerman::create(function () use ($chan) {
	var_dump("push pop");
	$ret = $chan->pop();
	var_dump($ret);
});

if (! defined("RUN_TEST")) {
	worker_event_wait();
}
