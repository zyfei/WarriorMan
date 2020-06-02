<?php
Workerman\Runtime::enableCoroutine();

$worker = new Workerman\Worker("tcp://0.0.0.0:8080", array(
	"backlog" => 1234,
	"count" => 2
));
$worker->onWorkerStart = function ($worker) {
	var_dump("onWorkerStart ->" . $worker->workerId);
};

$worker->onConnect = function ($connection) {
	// $connection->maxSendBufferSize = 0;
	// echo "new connection id {$connection->id} \n";
};
$worker->onMessage = function ($connection, $data) {
	$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello world\r\n";
	$connection->send($responseStr);
	// sleep(0.01);
};

$worker->onBufferFull = function ($connection) {
	echo "bufferFull and do not send again\n";
};

$worker->onError = function ($connection, $code, $msg) {
	var_dump($code);
	var_dump($msg);
	// echo "new connection id {$connection->id} \n";
};

$worker->onClose = function ($connection) {
	echo "connection closed\n";
};

$worker->run();
