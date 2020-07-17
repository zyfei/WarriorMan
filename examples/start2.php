<?php
require_once 'MySQL.php';

use Workerman\Worker;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数
                                       
// 创建一个Worker监听2345端口，使用http协议通讯
$http_worker = new Worker("tcp://0.0.0.0:2345");

// 启动4个进程对外提供服务
$http_worker->count = 4;
// 设置协议
$http_worker->protocol = "\Workerman\Protocols\Text";

// 接收到浏览器发送的数据时回复hello world给浏览器
$http_worker->onMessage = function ($connection, $data) {
	// 向浏览器发送hello world
	$connection->send('hello world');
};

// 运行worker
Worker::runAll();
