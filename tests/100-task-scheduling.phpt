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
var_dump(count($scheduler));

$result = $scheduler->run(function () use ($scheduler) {
	var_dump(count($scheduler));
	
	$t1 = Task::async('var_dump', 'B');
	$line = __LINE__ - 1;
	
	var_dump($t1->__debugInfo()['status']);
	var_dump($t1->__debugInfo()['suspended']);
	var_dump($t1->__debugInfo()['file'] == __FILE__);
	var_dump($t1->__debugInfo()['line'] == $line);
	
	var_dump(count($scheduler));
	
	var_dump('A');
	
	return 'C';
});

var_dump($result);

var_dump(count($scheduler));

?>
--EXPECT--
int(0)
int(0)
string(7) "PENDING"
bool(false)
bool(true)
bool(true)
int(1)
string(1) "A"
string(1) "B"
string(1) "C"
int(0)
