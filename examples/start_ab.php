<?php
use Workerman\Worker;
use Workerman\Lib\Timer;
use WOrkerman\Db\MysqlPool;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080");
$worker->count = 8;
$worker->name = "ab_test"; // 设置名字
$worker->mysqlPoll = NULL;
$worker->onWorkerStart = function ($worker) {
    $config = array();
    $config["host"] = "172.21.112.1";
    $config["port"] = 3306;
    $config["username"] = "www";
    $config["password"] = "www";
    $config["db_name"] = "test";

    $config["min"] = 10;
    $config["max"] = 30;
    $config["spareTime"] = 1;
    $worker->mysqlPoll = new MysqlPool($config);
};

$worker->onMessage = function ($connection, $data) use ($worker) {
    $db = $worker->mysqlPoll->get();
    $db->query("select now()");
    $worker->mysqlPoll->put($db);
    $responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
    $connection->send($responseStr);
};

Worker::runAll();
