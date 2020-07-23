<?php
use Warriorman\Coroutine;

work(function () {
    Coroutine::sleep(0.01);
    while (1) {
        for ($i = 0; $i < 100000; $i ++) {
            work(function () {
                $a = 1;
                echo ".";
            });
        }
        var_dump("sleep");
        Coroutine::sleep(1);
    }
});

Coroutine::wait();
