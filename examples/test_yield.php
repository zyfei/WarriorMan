<?php
use Warriorman\Coroutine;

/**
 * 展示协程切换
 */
// $arr[] = Coroutine::create(function () {
// var_dump(Coroutine::getCid() . " start");
// Coroutine::yield();
// var_dump(Coroutine::getCid() . " end");
// });

// $arr[] = work(function () {
// var_dump(Coroutine::getCid() . " start");
// Coroutine::yield();
// var_dump(Coroutine::getCid() . " end");
// });

// foreach ($arr as $n) {
// Coroutine::resume($n);
// }
work(function () {
    Coroutine::defer(function () {
        var_dump(1);
    });
    Coroutine::defer(function () {
        var_dump(2);
    });
});

Coroutine::wait();
