<?php
namespace Workerman\Db;

use Warriorman\Channel;

abstract class AbstractPool
{

    // 最少连接数
    private $min;

    // 最大连接数
    private $max;

    // 当前连接数
    private $count = 0;

    // 连接池组
    private $connections;

    // 用于空闲连接回收判断
    protected $spareTime;

    // 判断是否初始化了
    private $inited = false;

    // 连接配置
    protected $config = null;

    // 检测区间内，使用的连接峰值
    private $max_used_con_num = 0;

    protected abstract function createDb();

    protected abstract function checkDb($db);

    public function __construct($config)
    {
        $this->config = $config;
        $this->init($config);
    }

    /**
     * 创建资源
     */
    protected function createObject()
    {
        if ($this->count >= $this->max) {
            return false;
        }
        // 在协程切换之前加
        $this->count ++;
        $db = $this->createDb();
        if ($db) {
            $this->connections->push($db);
            return $db;
        }
        $this->count --;
        echo ("AbstractPool:" . T(time()) . " createObject error! \n");
        return false;
    }

    /**
     * 初始化最小数量连接池
     *
     * @return $this|null
     */
    protected function init($config = array())
    {
        if ($this->inited) {
            return null;
        }
        $this->config = $config;
        $this->min = key_exists("min", $config) ? $config["min"] : 2;
        $this->max = key_exists("max", $config) ? $config["max"] : 5;
        $this->spareTime = key_exists("spareTime", $config) ? $config["spareTime"] : 60;
        $this->connections = new Channel($this->max + 1);

        for ($i = 0; $i < $this->min; $i ++) {
            $this->createObject();
        }
        $this->inited = true;
        // 开始进行检测
        $this->gcSpareObject();
        return $this;
    }

    /**
     * 执行一个任务
     * 每次执行这个方法的时候判断是否需要减少mysql线程。 根据休眠时间看
     */
    public function execute($callback = null, $timeOut = 3)
    {
        // 连接数没达到最大，新建连接入池
        if ($this->connections->isEmpty()) {
            $this->createObject();
        }
        while (1) {
            $obj = $this->connections->pop($timeOut);
            if (! $obj) {
                echo ("AbstractPool:" . T(time()) . " connections empty! \n");
                return false;
            }
            // 如果发现这个连接失效了，那么去除，并新建一个
            if (! $this->checkDb($obj)) {
                $this->count --;
                unset($obj);
                $this->createObject();
                continue;
            }
            break;
        }
        $return_str = call_user_func($callback, $obj);

        // 计算当前使用峰值
        $_max_used_con_num = $this->count - $this->connections->length();
        if ($_max_used_con_num > $this->max_used_con_num) {
            $this->max_used_con_num = $_max_used_con_num;
        }

        $this->connections->push($obj);
        return $return_str;
    }

    /**
     * 得到一个连接池连接，必须free。
     */
    public function get($timeOut = 3)
    {
        // 连接数没达到最大，新建连接入池
        if ($this->connections->isEmpty()) {
            $this->createObject();
        }
        while (1) {
            $obj = $this->connections->pop($timeOut);
            if (! $obj) {
                echo ("AbstractPool:" . T(time()) . " connections empty! \n");
                return false;
            }
            // 如果发现这个连接失效了，那么去除，并新建一个
            if (! $this->checkDb($obj)) {
                $this->count --;
                unset($obj);
                $this->createObject();
                continue;
            }
            break;
        }
        // 计算当前使用峰值
        $_max_used_con_num = $this->count - $this->connections->length();
        if ($_max_used_con_num > $this->max_used_con_num) {
            $this->max_used_con_num = $_max_used_con_num;
        }
        return $obj;
    }

    /**
     * 归还连接
     *
     * @param unknown $obj
     */
    public function put($obj)
    {
        $this->connections->push($obj);
    }

    /**
     * 开始处理空闲连接
     */
    private function gcSpareObject()
    {
        // 大约1分钟检测一次连接
        // 回收算法，获得一分钟内被使用的连接峰值。当前count-峰值，如果大于等于2。那么释放一个连接
        \Warriorman\Lib\Timer::add($this->spareTime, function () {
            // 如果满足这个条件，那么释放一个连接
            if ($this->count > $this->min && ($this->count - $this->max_used_con_num) >= 2) {
                // 弹出一个资源，如果成功，那么去掉
                $obj = $this->connections->pop(0.001);
                if ($obj) {
                    $this->closeDb($obj);
                    $this->count --;
                }
            }
            $this->max_used_con_num = 0;
        });
    }
}