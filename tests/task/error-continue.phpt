--TEST--
Task will store error that failed it and pass it to continuations.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {
    try {
        Task::await(Task::async(function () {
            throw new \Error('Fail 1');
        }));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

try {
    TaskScheduler::run(function () {
        throw new \Error('Fail 2');
    });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(6) "Fail 1"
string(6) "Fail 2"
