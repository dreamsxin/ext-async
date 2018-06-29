--TEST--
Task activator is called whenever the first task is scheduled with a halted scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->activator(function () {
    var_dump('ACTIVATE!');
});

$work = function (string $v): void {
    var_dump($v);
};

$scheduler->task($work, ['A']);
$scheduler->run();

$scheduler->task($work, ['B']);
$scheduler->task($work, ['C']);
$scheduler->run();

$scheduler->task($work, ['D']);
$scheduler->task(function () use ($work) {
    Task::async($work, ['E']);
});
$scheduler->run();

?>
--EXPECTF--
string(9) "ACTIVATE!"
string(1) "A"
string(9) "ACTIVATE!"
string(1) "B"
string(1) "C"
string(9) "ACTIVATE!"
string(1) "D"
string(1) "E"
