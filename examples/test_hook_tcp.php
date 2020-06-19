<?php
/**
 * hook了系统函数
 * 目前支持
 * 	sleep
 */
Warriorman\Runtime::enableCoroutine();

Warriorman::create(function () {
// 	$ctx = stream_context_create([
// 		'socket' => [
// 			'so_reuseaddr' => true,
// 			'backlog' => 128
// 		]
// 	]);
// 	$socket = stream_socket_server('tcp://0.0.0.0:8080', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $ctx);
// 	if (! $socket) {
// 		echo "$errstr ($errno)" . PHP_EOL;
// 		exit(1);
// 	}
// 	var_dump($socket);
	
	
	$fp = stream_socket_client("tcp://127.0.0.1:8080", $errno, $errstr);
	if (!$fp) {
		echo 'error';
	} else {
		fwrite($fp,$message);
		fclose($fp);
		var_dump('--success--');
	}
	
	
});

if (! defined("RUN_TEST")) {
	worker_event_wait();
}