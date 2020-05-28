<?php
var_dump("00000000000000000000000000000000");

$worker = new Workerman\Server("tcp://0.0.0.0:8080", array(
	"backlog" => 1234,
	"count" => 2
));
$a = "1234";
$worker->onWorkerStart = function ($worker) use ($a) {
	var_dump("onWorkerStart ->" . $worker->workerId);
	var_dump($worker);
	var_dump($a);
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
