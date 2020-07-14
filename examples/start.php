<?php
require_once 'MySQL.php';

use Workerman\Worker;
use Workerman\Lib\Timer;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080", array(
	"backlog" => 1234, // 默认102400，等待accept的连接队列长度
	"count" => 1 // 进程数量
));

$worker->name = "tcpServer"; // 设置名字
$worker->protocol = "\Workerman\Protocols\Text";

$worker->onWorkerStart = function ($worker) {
	var_dump("onWorkerStart ->" . $worker->workerId);
	global $db;
	$db = new test\MySQL("127.0.0.1", "3306", "root", "root", "qipai_pingtai");
	
	$timer_id = Timer::add(0.01, function () {
		echo "Timer run \n";
	}, false);
};

$worker->onConnect = function ($connection) {
	$connection->set(array(
		"maxSendBufferSize" => 102400
	));
	echo "new connection id {$connection->id} \n";
};
global $aa;
$aa = 0;
$worker->onMessage = function ($connection, $data) {
// 	global $aa;
// 	$aa ++;
// 	// global $db;
// 	// $len = $db->query("select count(*) from a_agent");
// 	// var_dump($len);
// 	if ($aa / 2 == 0) {
// 		$_GET = array();
// 	} else {
// 		$_GET = NULL;
// 	}
// 	sleep(1);
// 	var_dump($_GET);
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
$worker2 = new Worker("tcp://0.0.0.0:8081", array(
	"backlog" => 1234, // 默认102400，等待accept的连接队列长度
	"count" => 1 // 进程数量
));

$worker2->protocol = "\Workerman\Protocols\Text";

$worker2->onMessage = function ($connection, $data) {
	$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
	$connection->send($responseStr);
};

// 监听另外一个端口
$worker3 = new Worker("udp://0.0.0.0:8080", array(
	"count" => 4 // 进程数量
));
$worker3->onMessage = function ($connection, $data) {
	var_dump("udp:" . $data);
	$connection->send("hello world");
};

Worker::runAll();
