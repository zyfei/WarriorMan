<?php
require_once 'MySQL.php';

Swoole\Runtime::enableCoroutine($flags = SWOOLE_HOOK_ALL);
Co\run(function () {
	global $db;
	$db = new test\MySQL("127.0.0.1", "3306", "root", "root", "qipai_pingtai");
	$len = $db->query("select count(*) from a_agent where id=1");
	var_dump($len);
	
	$server = new Co\Http\Server("127.0.0.1", 8080, false);
	$server->handle('/', function ($request, $response) {
		$response->end("<h1>Index</h1>");
		global $db;
		$len = $db->query("select sleep(3)");
		var_dump($len);
	});
	$server->start();
});