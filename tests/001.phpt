--TEST--
Check if workerman is loaded
--SKIPIF--
<?php
if (!extension_loaded('workerman')) {
	echo 'skip';
}
?>
--FILE--
<?php
echo 'The extension "workerman" is available';
?>
--EXPECT--
The extension "workerman" is available
