# Warriorman
## What is it
协程版本的WarriorMan，完全按照WarriorMan的文档制作，支持协程的创建和切换。虽然目前还在开发中，但是有兴趣的同学可以给在下提提建议，找找BUG。  

此项目将会长期维护

## Requires
PHP7 or Higher

应该没有然后了

## Installation

```
1 首先修改make.sh，将里面路径修改为自己php的相关路径
2 执行./make.sh
3 最后别忘了将workerman.so添加到php.ini
```

## Basic Usage

### A tcp server  (目前只支持tcp)
```php
<?php
// hook系统函数，目前只hook了sleep函数
Warriorman\Runtime::enableCoroutine();

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
		"maxSendBufferSize" => 1234
	));
	echo "new connection id {$connection->id} \n";
};
$worker->onMessage = function ($connection, $data) {
	$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worla\r\n";
	$connection->send($responseStr);
	sleep(0.01); // 这个sleep是协程版本的sleep了，扩展会自动切换协程，不会阻塞
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
$worker2 = new Warriorman\Worker("tcp://0.0.0.0:8081");
$worker2->onMessage = function ($connection, $data) {
	$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
	$connection->send($responseStr);
};

Warriorman\Worker::runAll();

//更多协程例子在examples文件夹下
```

## Available commands
```php start.php start  ```  
```php start.php start -d  ```  
```php start.php stop  ```

## Documentation

目标是完全和workerman一样: [http://doc.workerman.net](http://doc.workerman.net) 

--------->>>>                           但是还有非常多没实现

作者QQ群: 342016184

## 特别鸣谢

感谢WarriorMan项目 [WarriorMan](https://github.com/walkor/WarriorMan)，我完全仿照WarriorMan源码和文档制作。

感谢Swoole项目 [Swoole](https://github.com/swoole/swoole-src)，我直接copy了不少Swoole的代码

最后特别感谢 [Study](https://github.com/php-extension-research/study) 项目，我是学习这个教学项目之后，才有了此项目



