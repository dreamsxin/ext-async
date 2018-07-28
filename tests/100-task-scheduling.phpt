--TEST--
Task scheduling and running using the scheduler API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$result = $scheduler->run(function () use ($scheduler) {
	$t1 = Task::async('var_dump', 'B');
	$line = __LINE__ - 1;
	
	var_dump($t1->__debugInfo()['status']);
	var_dump($t1->__debugInfo()['suspended']);
	var_dump($t1->__debugInfo()['file'] == __FILE__);
	var_dump($t1->__debugInfo()['line'] == $line);
	
	var_dump('A');
	
	return 'C';
});

var_dump($result);

?>
--EXPECT--
string(7) "PENDING"
bool(false)
bool(true)
bool(true)
string(1) "A"
string(1) "B"
string(1) "C"
