<?php
require_once 'MySQL.php';

Swoole\Runtime::enableCoroutine($flags = SWOOLE_HOOK_ALL);
Co\run(function () {
	
	$server = new Co\Http\Server("127.0.0.1", 8080, false);
	$server->handle('/', function ($request, $response) {
		$response->end("<h1>Index</h1>");
	});
	$server->start();
});