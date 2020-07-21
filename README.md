# 序言
WarriorMan是一款php的协程高性能socket扩展，适合对 [Workerman](https://www.workerman.net/) 有一定的了解的同学使用。

## WarriorMan是什么
WarriorMan是一个完全用c语言编写的php扩展，按照 [Workerman](https://www.workerman.net/) 的 [手册](http://doc.workerman.net/) 制作，解决Workerman的一些短板，为Workerman插上协程的翅膀。

### 作者希望
作者在这里希望大家可以尝试使用WarriorMan，尝试学习WarriorMan源码，源码中有大量中文注释，这对提升自己编程水平以及更好的理解Workerman和Swoole很有帮助。  
如果在使用/学习中遇到任何问题，可以提issues或者在QQ群: 1098698769 中直接联系作者
  
## WarriorMan与WorkerMan的不同
### 缺点
1 WarriorMan没有WorkerMan稳定，目前适合学习与尝试。  
2 WarriorMan如果框架本身出现BUG，对于不懂PHP扩展调试的开发者，那么只能等待WarriorMan开发者们修复。
### 优点
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
use Warriorman\Worker;
use Warriorman\Runtime;

Worker::rename(); // 为了防止命名空间冲突
Runtime::enableCoroutine(); // hook相关函数

$worker = new Worker("tcp://0.0.0.0:8080");


$worker->onMessage = function ($connection, $data) {
	$responseStr = "hello world";
	$connection->send($responseStr);
};

Worker::runAll();
```

## 文档
WarriorMan:[https://www.kancloud.cn/wwwoooshizha/warriorman/content](https://www.kancloud.cn/wwwoooshizha/warriorman/content)  
文档是在Workerman文档基础上划出两者不同，和独有的一些功能。  

## 交流
WarriorMan 交流QQ群: 1098698769

## 特别鸣谢
[Workerman](https://github.com/walkor/Workerman)  
[Swoole](https://github.com/swoole/swoole-src)  
[Study](https://github.com/php-extension-research/study)  
