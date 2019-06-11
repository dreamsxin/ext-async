--TEST--
Task scheduling and running using the scheduler API.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$result = TaskScheduler::run(function () {
	$t1 = Task::async('var_dump', 'B');
	$line = __LINE__ - 1;
	
	print_r($t1);
	
	var_dump('A');
	
	return 'C';
});

var_dump($result);

?>
--EXPECTF--
Concurrent\Task Object
(
    [status] => PENDING
    [file] => %s
    [line] => %d
)
string(1) "A"
string(1) "B"
string(1) "C"
