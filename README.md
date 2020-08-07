# 序言
WarriorMan是一款php的协程高性能socket扩展，适合对 [Workerman](https://www.workerman.net/) 或者 [swoole](https://github.com/swoole/swoole-src) 有一定的了解的同学使用。

## WarriorMan是什么
WarriorMan是一个完全用c语言编写的php扩展，仿照 [Workerman](https://www.workerman.net/) 制作，解决Workerman的一些短板，为Workerman插上协程的翅膀。

## WarriorMan与WorkerMan的不同
### 缺点
1 WarriorMan没有WorkerMan稳定。  
2 WarriorMan扩展是用纯C编写，阅读调试有一定难度。
### 优点
1 WarriorMan提供协程调度方法，默认的事件回调也是通过协程调度实现，可以在IO操作方面节省大量时间。
2 WarriorMan因为HOOK了PHP TCP Socket 类型的 stream，所以常见的`Redis`、`PDO`、`Mysqli`以及用 PHP 的[streams](https://www.php.net/streams)系列函数操作 TCP 连接的操作，都默认支持协程调度，减少了编程复杂度。  

3 会逐步提供更多底层方法，为开发者提供更多的灵活度。  

## WarriorMan适合什么样的人
如果你初次接触socket长连接编程，建议使用 [Workerman](https://www.workerman.net/) 框架开发。  
如果你的项目业务逻辑很清晰，没有频繁的IO操作，建议使用 [Workerman](https://www.workerman.net/) 框架开发。  
如果你的项目IO操作很频繁，可以考虑使用WarriorMan  
如果你对C语言感兴趣，对协程原理感兴趣，可以考虑使用WarriorMan  
如果你是一个热于折腾的人，并且可以主导自己的项目，可以考虑使用WarriorMan  
如果你对php扩展开发有经验，请一定尝试使用WarriorMan 

## 压力测试
[压力测试](https://www.kancloud.cn/wwwoooshizha/warriorman/1839724)  
[压力测试-数据库](https://www.kancloud.cn/wwwoooshizha/warriorman/1839725)

### 作者希望
作者在这里希望大家可以尝试使用WarriorMan，尝试学习WarriorMan源码，源码中有大量中文注释，这对提升自己编程水平以及更好的理解Workerman和Swoole很有帮助。  
如果在使用/学习中遇到任何问题，可以提issues或者在QQ群: 1098698769 中直接联系作者  
目前第二版学习制作中，作者目前正在学习Rust，预计融合Rust的思想。或者是使用Rust制作php容器

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
文档是直接用了WorkerMan和Swoole的部分文档  

## 交流
WarriorMan 交流QQ群: 1098698769

## 特别鸣谢
[Workerman](https://github.com/walkor/Workerman)  
[Swoole](https://github.com/swoole/swoole-src)  
[Study](https://github.com/php-extension-research/study)  

## 友情链接
[爬虫-爬山虎](https://github.com/blogdaren/PHPCreeper)  

