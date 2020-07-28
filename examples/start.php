<?php
use Workerman\Worker;
use Workerman\Lib\Timer;
use WOrkerman\Db\MysqlPool;

require_once 'Workerman/Autoloader.php';

Warriorman\Worker::rename(); // 将Workerman改为Workerman
Warriorman\Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080");
$worker->count = 1;
$worker->name = "tcpServer"; // 设置名字
$worker->protocol = "\Workerman\Protocols\Http"; // 设置协议
$worker->mysqlPoll = NULL;
$worker->onWorkerStart = function ($worker) {
    $config = array();
    $config["host"] = "192.168.2.103";
    $config["port"] = 3306;
    $config["username"] = "www";
    $config["password"] = "www";
    $config["db_name"] = "test";

    $config["min"] = 2;
    $config["max"] = 100;
    $config["spareTime"] = 1;

    $worker->mysqlPoll = new MysqlPool($config);

    var_dump("onWorkerStart ->" . $worker->workerId . " id=" . $worker->id);
};

$worker->onWorkerReload = function ($worker) {
    var_dump("onWorkerReload ->" . $worker->id);
};
$worker->onConnect = function ($connection) use ($worker) {
    $connection->set(array(
        "maxSendBufferSize" => 102400
    ));
    $ip = $connection->getRemoteIp();
    $port = $connection->getRemotePort();
    echo "new connection id {$connection->id} ip=$ip port=$port\n";
};
$worker->onMessage = function ($connection, $data) use ($worker) {
    $db = $worker->mysqlPoll->get();
    $responseStr = json_encode($db->query("select now()"));
    $worker->mysqlPoll->put($db);
    $connection->close($responseStr);
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
$worker2->count = 1;
$worker2->onMessage = function ($connection, $data) {
    $responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
    $connection->send($responseStr);
};

// 监听另外一个端口
$worker3 = new Worker("udp://0.0.0.0:8080");
$worker3->onMessage = function ($connection, $data) {
    var_dump("udp:" . $data);
    $connection->send("hello world");
    $ip = $connection->getRemoteIp();
    $port = $connection->getRemotePort();
    echo "new connection id {$connection->id} ip=$ip port=$port\n";
};

Worker::runAll();
