--TEST--
Task will schedule an error if an awaited object is disposed before it is resolved.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
    Task::await((new Deferred())->awaitable());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async(function () {
        return Task::await((new Deferred())->awaitable());
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async(function () {
        return Task::await(Task::async(function () {
            return Task::await((new Deferred())->awaitable());
        }));
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(31) "Awaitable has not been resolved"
string(31) "Awaitable has not been resolved"
string(31) "Awaitable has not been resolved"
