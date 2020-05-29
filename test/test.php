<?php
$worker = new Workerman\Worker("tcp://0.0.0.0:8080", array(
	"backlog" => 1234,
	"count" => 2
));
$worker->onWorkerStart = function ($worker) {
	//var_dump("onWorkerStart ->" . $worker->workerId);
};

$worker->onConnect = function ($connection) {
	//echo "new connection id  {$connection->id} \n";
};
$cid = 0;
$worker->onMessage = function ($connection, $data) use(&$cid) {
	$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: 11\r\n\r\nhello world\r\n";
	$connection->send($responseStr);
	$connection->close();
//	echo "new onMessage id  {$connection->id} start \n";
// 	if($connection->id==1){
// 		$cid = worker_coroutine::getCid();
// 		var_dump("---->".$cid);
// 		worker_coroutine::yield();
// 	}
// 	if($connection->id==2){
// 		var_dump("22222222  >".$cid);
// 		worker_coroutine::resume($cid);
// 	}
//	echo "new onMessage id  {$connection->id} $data \n";
	//$connection->send('receive success');
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
