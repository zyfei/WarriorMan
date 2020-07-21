<?php
require_once 'MySQL.php';

use Workerman\Worker;
use Workerman\Lib\Timer;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080");
$worker->count = 2;
$worker->name = "tcpServer"; // 设置名字
$worker->protocol = "\Workerman\Protocols\Http"; // 设置协议
$worker->onWorkerStart = function ($worker) {
    var_dump("onWorkerStart ->" . $worker->workerId . " id=" . $worker->id);
    global $db;
    $db = new test\MySQL("127.0.0.1", "3306", "root", "root", "test");

    $timer_id = Timer::add(1, function () {
        echo "coro_num = " . Warriorman\Coroutine::getTotalNum() . " \n";
    }, false);
    $inner_text_worker = new Worker('tcp://0.0.0.0:5678');
    $inner_text_worker->reusePort = true;
    $inner_text_worker->protocol = "\Workerman\Protocols\Http"; // 设置协议
    $inner_text_worker->onWorkerStart = function ($worker) {
        var_dump("inner_text_worker");
    };
    $inner_text_worker->onMessage = function ($connection, $buffer) {
        $connection->send("inner_text_worker");
    };
    
    // ## 执行监听 ##
    $inner_text_worker->listen();
};

$worker->onWorkerReload = function ($worker) {
    var_dump("onWorkerReload ->" . $worker->id);
};

$worker->onConnect = function ($connection) use ($worker) {
    $connection->set(array(
        "maxSendBufferSize" => 102400
    ));
    echo "new connection id {$connection->id} \n";
};

$worker->onMessage = function ($connection, $data) {
    // var_dump($data);
    // $responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worla\r\n";
    $responseStr = "hello worla";
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
$worker2 = new Worker("tcp://0.0.0.0:8081");
// $worker2->protocol = "\Workerman\Protocols\Http"; // 设置协议

$worker2->onMessage = function ($connection, $data) {
    $responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
    $connection->send($responseStr);
};

// 监听另外一个端口
$worker3 = new Worker("udp://0.0.0.0:8080");
$worker3->onMessage = function ($connection, $data) {
    var_dump("udp:" . $data);
    $connection->send("hello world");
};

Worker::runAll();
