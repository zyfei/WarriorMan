<?php

// 高性能HTTP服务器
$http = new Swoole\Http\Server("127.0.0.1", 8081);

$http->on("start", function ($worker) {
	echo "Swoole http worker is started at http://127.0.0.1:8080\n";
});

$http->on("request", function ($request, $response) {
	$response->header("Content-Type", "text/plain");
	$response->end("Hello World\n");
});

$http->start();