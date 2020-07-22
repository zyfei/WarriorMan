<?php
use Warriorman\Coroutine;

work(function () {
    var_dump(Coroutine::getCid());
    Coroutine::sleep(1);
    work(function () {
        var_dump(Coroutine::getCid());
    });
    var_dump(Coroutine::getCid());
});

Coroutine::create(function () {
    Coroutine::sleep(1);
    var_dump(Coroutine::getCid());
});

Coroutine::wait();
