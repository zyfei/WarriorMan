<?php

Workerman\Runtime::enableCoroutine();

$worker = new Workerman\Worker("tcp://0.0.0.0:8080", array(
	"backlog" => 1234,
	"count" => 2
));
$worker->onWorkerStart = function ($worker) {
	// var_dump("onWorkerStart ->" . $worker->workerId);
};

$cid = 0;
$worker->onConnect = function ($connection) use ($cid) {
	//echo "new connection id  {$connection->id} \n";
	
};
$cid = 0;
$worker->onMessage = function ($connection, $data) use ($cid) {
	$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello world\r\n";
	$connection->send($responseStr);
	//$connection->close();
	//var_dump("onMessage over");
	//sleep(0.01);
};

$worker->onClose = function ($connection) {
	echo "connection closed\n";
};

// $worker->set_handler(function (Workerman\Socket $conn) use ($worker) {
// $data = $conn->recv();
// if ($data == false) {
// return;
// }
// $responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: 11\r\n\r\nhello world\r\n";
// $conn->send($responseStr);
// $conn->close();
// });

$worker->run();
