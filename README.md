# WarriorMan
## What is it
协程版本的WarriorMan，完全按照WarriorMan的文档制作，协程同步的C编码风格，支持协程的创建和切换。虽然目前还在开发中，但是有兴趣的同学可以给在下提提建议，找找BUG。  
已经初步的hook了tcp相关操作，使pdo redis等PHP自带的客户端默认使用协程特性，模拟mysql查询成功。另外此项目将会长期维护   

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
Warriorman::create(function () {
	$worker = new Warriorman\Worker("tcp://0.0.0.0:8080", array(
		"backlog" => 1234, // 默认102400，等待accept的连接队列长度
		"count" => 1 // 进程数量
	));
	$worker->name = "tcpServer"; // 设置名字
	
	$worker->onWorkerStart = function ($worker) {
		var_dump("onWorkerStart ->" . $worker->workerId);
	};
	
	$worker->onConnect = function ($connection) {
		$connection->set(array(
			"maxSendBufferSize" => 102400
		));
		echo "new connection id {$connection->id} \n";
	};
	
	$worker->onMessage = function ($connection, $data) {
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
	$worker2 = new Warriorman\Worker("tcp://0.0.0.0:8081", array(
		"backlog" => 1234, // 默认102400，等待accept的连接队列长度
		"count" => 8 // 进程数量
	));
	$worker2->onMessage = function ($connection, $data) {
		$responseStr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: Keep-Alive\r\nContent-Length: 11\r\n\r\nhello worlb\r\n";
		$connection->send($responseStr);
	};
	
	Warriorman\Worker::runAll();
});

//更多协程例子在examples文件夹下
```

## Available commands
```php start.php start  ```  
```php start.php start -d  ```  
```php start.php stop  ```

## Documentation

目标是完全和workerman的文档一样: [http://doc.workerman.net](http://doc.workerman.net) 

--------->>>>                           但是还有非常多没实现

作者QQ群: 342016184

## 特别鸣谢

感谢Workerman项目 [Workerman](https://github.com/walkor/Workerman)，我完全依照Workerman文档制作。
感谢Swoole项目 [Swoole](https://github.com/swoole/swoole-src)，给了我很多思路，并且我直接copy了不少Swoole的代码
最后特别感谢 [Study](https://github.com/php-extension-research/study) 项目，我是学习这个教学项目之后，才有了此项目



