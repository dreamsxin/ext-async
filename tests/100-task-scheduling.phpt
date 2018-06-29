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

$t1 = $scheduler->task(function () {
    var_dump('A');
});

$scheduler->task(function () use ($scheduler) {
    $scheduler->task(function () {
        var_dump('C');
    });

    var_dump('B');
});

var_dump($t1 instanceof Task);
var_dump(count($scheduler));

$scheduler->run();

var_dump(count($scheduler));

?>
--EXPECTF--
int(0)
bool(true)
int(2)
string(1) "A"
string(1) "B"
string(1) "C"
int(0)
