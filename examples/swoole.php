<?php
require_once 'MySQL.php';

Swoole\Runtime::enableCoroutine($flags = SWOOLE_HOOK_ALL);
// Co\run(function () {

// $server = new Co\Http\Server("127.0.0.1", 8081, false);
// $server->handle('/', function ($request, $response) {
// $response->end("<h1>Index</h1>");
// });
// $server->start();
// });

$server = new Swoole\Server("127.0.0.1", 8081);

$server->on('receive', function ($server, $fd, $reactor_id, $data) {
    $responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
    $server->send($fd, $responseStr);
});


$server->start();