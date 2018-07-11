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
    return Task::await((new Deferred())->awaitable());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    return Task::await(Task::async(function () {
        return Task::await((new Deferred())->awaitable());
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

/*
try {
    return Task::await(Task::async(function () {
        return Task::await(Task::async(function () {
            return Task::await((new Deferred())->awaitable());
        }));
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}
*/

?>
--EXPECT--
string(39) "Awaitable was not resolved during await"
string(38) "Awaited task did not run to completion"
