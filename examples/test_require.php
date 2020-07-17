<?php
use Workerman\Worker;
use Workerman\Lib\Timer;

require_once 'Workerman/Autoloader.php';
Warriorman\Worker::rename();

// worker实例1有4个进程，进程id编号将分别为0、1、2、3
$worker1 = new Worker('tcp://0.0.0.0:8585');
// 设置启动4个进程
$worker1->count = 4;
// 每个进程启动后打印当前进程id编号即 $worker1->id
$worker1->onWorkerStart = function ($worker1) {
	echo "worker1->id={$worker1->id}\n";
};

// worker实例2有两个进程，进程id编号将分别为0、1
$worker2 = new Worker('tcp://0.0.0.0:8686');
// 设置启动2个进程
$worker2->count = 2;
// 每个进程启动后打印当前进程id编号即 $worker2->id
$worker2->onWorkerStart = function ($worker2) {
	echo "worker2->id={$worker2->id}\n";
};

// 运行worker
Worker::runAll();