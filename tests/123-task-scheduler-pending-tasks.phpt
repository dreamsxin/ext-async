--TEST--
Task scheduler provides access to pending tasks.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {}, function (TaskScheduler $scheduler) {
    var_dump(empty($scheduler->getPendingTasks()));
});

TaskScheduler::run(function () {
    $defer = new Deferred();

    $t = Task::async(function () use ($defer) {
        Task::await($defer->awaitable());
    });
}, function (TaskScheduler $scheduler) {
    array_map(function (Task $t) {
        var_dump($t->__debugInfo()['status']);
        var_dump($t->__debugInfo()['suspended']);
    }, $scheduler->getPendingTasks());
});

--EXPECT--
bool(true)
string(7) "PENDING"
bool(false)
