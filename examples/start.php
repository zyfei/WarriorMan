<?php
Warriorman\Runtime::enableCoroutine();
Warriorman::create(function () {
	$worker = new Warriorman\Worker("tcp://0.0.0.0:8080", array(
		"backlog" => 1234, // 默认102400，等待accept的连接队列长度
		"count" => 2 // 进程数量
	));
	$worker->name = "tcpServer"; // 设置名字
	
	$worker->onWorkerStart = function ($worker) {
		var_dump("onWorkerStart ->" . $worker->workerId);
	};
	
	$worker->onConnect = function ($connection) {
		$connection->set(array(
			"maxSendBufferSize" => 102400
		));
		echo "new connection id {$connection->id} \n";
	};
	
	$worker->onMessage = function ($connection, $data) {
		$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worla\r\n";
		$connection->send($responseStr);
	};
	
	$worker->onBufferFull = function ($connection) {
		echo "bufferFull and do not send again\n";
	};
	
	$worker->onError = function ($connection, $code, $msg) {
		var_dump($code);
		var_dump($msg);
		echo "connection error ,id {$connection->id} \n";
	};
	
	$worker->onClose = function ($connection) {
		echo "connection closed\n";
	};
	
	// 监听另外一个端口
	$worker2 = new Warriorman\Worker("tcp://0.0.0.0:8081", array(
		"backlog" => 1234, // 默认102400，等待accept的连接队列长度
		"count" => 8 // 进程数量
	));
	$worker2->onMessage = function ($connection, $data) {
		$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
		$connection->send($responseStr);
	};
	
	Warriorman\Worker::runAll();
});
