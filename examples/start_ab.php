<?php
use Workerman\Worker;
use Workerman\Lib\Timer;
use WOrkerman\Db\MysqlPool;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080");
$worker->count = 1;
$worker->name = "ab测试"; // 设置名字
$worker->protocol = "\Workerman\Protocols\Http"; // 设置协议
$worker->mysqlPoll = NULL;
$worker->onWorkerStart = function ($worker) {
    $config = array();
    $config["host"] = "192.168.2.103";
    $config["port"] = 3306;
    $config["username"] = "www";
    $config["password"] = "www";
    $config["db_name"] = "test";

    $config["min"] = 10;
    $config["max"] = 30;
    $config["spareTime"] = 1;
    $worker->mysqlPoll = new MysqlPool($config);
    
    var_dump("onWorkerStart");
};

$worker->onMessage = function ($connection, $data) use ($worker) {
    $db = $worker->mysqlPoll->get();
    $responseStr = json_encode($db->query("select now()"));
    $worker->mysqlPoll->put($db);
    $connection->send($responseStr);
};

Worker::runAll();
