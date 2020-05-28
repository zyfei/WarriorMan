<?php
$worker = new Workerman\Worker("tcp://0.0.0.0:8080", array(
	"backlog" => 1234,
	"count" => 2
));
$worker->onWorkerStart = function ($worker) {
	var_dump("onWorkerStart ->" . $worker->workerId);
};

$worker->onConnect = function ($connection) {
	var_dump($connection);
	echo "new connection id  {$connection->id} \n";
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
