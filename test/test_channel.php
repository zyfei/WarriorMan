<?php
$chan = new Corkerman\Channel(1);
var_dump($chan);
worker_go(function () use ($chan) {
	var_dump("push start");
	$ret = $chan->push("hello world");
	var_dump($ret);
});

worker_go(function () use ($chan) {
	var_dump("push pop");
	$ret = $chan->pop();
	var_dump($ret);
});

if (! defined("RUN_TEST")) {
	worker_event_wait();
}
