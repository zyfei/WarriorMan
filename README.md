# 序言
WarriorMan是一款php的协程高性能socket扩展

## WarriorMan是什么
WarriorMan是一个完全用c语言编写的php扩展，按照 [Workerman](https://www.workerman.net/) 的 [手册](http://doc.workerman.net/) 制作，解决Workerman的一些短板，为Workerman插上协程的翅膀。
  
## WarriorMan与WorkerMan的不同
1 Workerman是纯php实现的网络框架，WarriorMan是纯c实现的php扩展
2 Workerman的事件是基于异步回调的编码方式实现的，WarriorMan是协程同步的编码方式实现
3 Workerman的mysql客户端，redis客户端如果要实现非阻塞，依赖于基于异步回调的第三方库。而WarriorMan因为HOOK了PHP TCP Socket 类型的 stream，所以常见的`Redis`、`PDO`、`Mysqli`以及用 PHP 的[streams](https://www.php.net/streams)系列函数操作 TCP 连接的操作，都默认支持协程调度，减少了编程复杂度。

## 环境
PHP7 or Higher

## 安装
```
1 首先修改make.sh，将里面路径修改为自己php的路径
2 执行./make.sh
3 最后别忘了将workerman.so添加到php.ini
```
### A tcp server
```php
use Workerman\Worker;
use Workerman\Lib\Timer;

Warriorman\Worker::rename(); // 为了防止命名空间冲突
Warriorman\Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080", array(
	"backlog" => 1234, // 默认102400，等待accept的连接队列长度
	"count" => 1 // 进程数量
));

$worker->name = "tcpServer"; // 设置名字
$worker->protocol = "\Workerman\Protocols\Http"; // 设置协议

$worker->onWorkerStart = function ($worker) {
	var_dump("onWorkerStart ->" . $worker->workerId);
    
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

$worker->onMessage = function ($connection, $data) {
	$responseStr = "hello world";
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
$worker3 = new Worker("udp://0.0.0.0:8080");
$worker3->onMessage = function ($connection, $data) {
	$connection->send("hello world");
};

Worker::runAll();
```

## 文档
WarriorMan:[https://www.kancloud.cn/wwwoooshizha/warriorman/content](https://www.kancloud.cn/wwwoooshizha/warriorman/content)
WorkerMan:[http://doc.workerman.net](http://doc.workerman.net/)

## 交流
WarriorMan 交流QQ群: 1098698769

## 特别鸣谢
[Workerman](https://github.com/walkor/Workerman)  
[Swoole](https://github.com/swoole/swoole-src)  
[Study](https://github.com/php-extension-research/study)  
