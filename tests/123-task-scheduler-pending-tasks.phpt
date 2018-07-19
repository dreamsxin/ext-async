--TEST--
Task scheduler provides access to pending tasks.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register($scheduler = new TaskScheduler());

var_dump(empty($scheduler->getPendingTasks()));

$defer = new Deferred();

$t = Task::async(function () use ($defer) {
    Task::await($defer->awaitable());
});

var_dump(empty($scheduler->getPendingTasks()));

$x = Task::async(function () use ($scheduler) {
    array_map(function (Task $t) {
        var_dump($t->__debugInfo()['status']);
        var_dump($t->__debugInfo()['suspended']);
    }, $scheduler->getPendingTasks());
});

Task::await($x);

var_dump(empty($scheduler->getPendingTasks()));

--EXPECT--
bool(true)
bool(true)
string(7) "PENDING"
bool(true)
bool(false)
