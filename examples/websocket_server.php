<?php
use Workerman\Worker;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数
                                       
// 创建一个Worker监听2345端口，使用http协议通讯
$ws_worker = new Worker("tcp://0.0.0.0:2345");

// 启动4个进程对外提供服务
$ws_worker->count = 4;
// 设置协议
$ws_worker->protocol = "\Workerman\Protocols\Websocket";

// 接收到浏览器发送的数据时回复hello world给浏览器
$ws_worker->onMessage = function ($connection, $data) {
	// 向浏览器发送hello world
	$connection->send('hello world');
};

// 运行worker
Worker::runAll();
