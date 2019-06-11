--TEST--
Task scheduler provides access to pending tasks.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {}, function (array $tasks) {
    var_dump(empty($tasks));
});

TaskScheduler::run(function () {
    $defer = new Deferred();
    $a = $defer->awaitable();

    $t = Task::async(function () use ($a) {
        Task::await($a);
    });
}, function (array $tasks) {
    array_map(function (Task $task) {
        var_dump($task->status);
    }, $tasks);
});

--EXPECT--
bool(true)
string(7) "PENDING"
