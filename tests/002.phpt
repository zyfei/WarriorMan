--TEST--
workerman_test1() Basic test
--SKIPIF--
<?php
if (!extension_loaded('workerman')) {
	echo 'skip';
}
?>
--FILE--
<?php
$ret = workerman_test1();

var_dump($ret);
?>
--EXPECT--
The extension workerman is loaded and working!
NULL
