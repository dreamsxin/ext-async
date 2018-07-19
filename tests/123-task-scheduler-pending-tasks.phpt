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
$line = __LINE__ - 1;

var_dump(empty($scheduler->getPendingTasks()));

$x = Task::async(function () use ($scheduler, $line) {
    array_map(function (Task $t) use ($line) {
        var_dump(strlen($t->getId()));
        var_dump($t->getFile() == __FILE__);
        var_dump($t->getLine() == $line);
    }, $scheduler->getPendingTasks());
});

Task::await($x);

var_dump(empty($scheduler->getPendingTasks()));

--EXPECT--
bool(true)
bool(true)
int(16)
bool(true)
bool(true)
bool(false)
