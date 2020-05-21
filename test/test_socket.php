<?php
$cid = worker_go(function () {
	$cid = Workerman::getCid();
	echo "coroutine [$cid] create" . PHP_EOL;
	$socket = new Workerman\Socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	$socket->bind("127.0.0.1", 8080);
	$a = $socket->listen(128);
	while (1) {
		$conn = $socket->accept();
		worker_go(function () use ($conn) {
			$msg = $conn->recv();
			if ($msg == false) {
				return;
			}
			$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: 11\r\n\r\nhello world\r\n";
			$conn->send($responseStr);
			$conn->close();
		});
	}
});
worker_event_wait();