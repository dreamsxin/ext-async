--TEST--
Task scheduler provides access to pending tasks.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {}, function (array $tasks) {
    var_dump(empty($tasks));
});

TaskScheduler::run(function () {
    $defer = new Deferred();

    $t = Task::async(function () use ($defer) {
        Task::await($defer->awaitable());
    });
}, function (array $tasks) {
    array_map(function (array $info) {
        var_dump($info['status']);
        var_dump($info['suspended']);
    }, $tasks);
});

--EXPECT--
bool(true)
string(7) "PENDING"
bool(false)
