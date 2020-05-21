<?php
$server = null;
worker_go(function () {
	$server = new Workerman\Server("127.0.0.1", 8080);
	
	$server->set_handler(function (Workerman\Socket $conn) use ($server) {
		$data = $conn->recv();
		if ($data == false) {
			return;
		}
		$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: 11\r\n\r\nhello world\r\n";
		$conn->send($responseStr);
		$conn->close();
		// worker::sleep(0.01);
	});
	
	$server->run();
	
	var_dump("a si le");
});

worker_event_wait();