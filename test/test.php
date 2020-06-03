<?php
Corkerman\Runtime::enableCoroutine();

require_once 'test_worker.php';

Corkerman\Worker::runAll();