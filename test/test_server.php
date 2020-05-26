<?php
$cid = worker_go(function () {
	$server = new Workerman\Server("127.0.0.1", array(
		"backlog" => 1234,
		"count" => 2
	));
	
	$server->onWorkerStart = function ($server) {
		var_dump("onWorkerStart ->" . $server->workerId);
	};
	
	$server->set_handler(function (Workerman\Socket $conn) use ($server) {
		$data = $conn->recv();
		if ($data == false) {
			return;
		}
		$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: 11\r\n\r\nhello world\r\n";
		$conn->send($responseStr);
		$conn->close();
	});
	$server->run();
});
worker_event_wait();