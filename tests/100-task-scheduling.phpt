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
int(1)
string(1) "A"
string(1) "B"
string(1) "C"
int(0)
