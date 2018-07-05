--TEST--
Task indicates if currently executed code is running inside a task.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$f = new Fiber(function () {
    var_dump(Fiber::isRunning(), Task::isRunning());
});

$scheduler->task(function () {
    var_dump(Fiber::isRunning(), Task::isRunning());
})->continueWith(function () {
    var_dump(Task::isRunning());
});

var_dump(Task::isRunning());

$scheduler->run();

var_dump(Task::isRunning());

$f->start();

var_dump(Task::isRunning());

?>
--EXPECTF--
bool(false)
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
