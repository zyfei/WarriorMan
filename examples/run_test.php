<?php
define ( 'RUN_TEST', 1 );

require_once 'test_channel.php';
require_once 'test_hook_sleep.php';
require_once 'test_yield.php';

worker_event_wait();
