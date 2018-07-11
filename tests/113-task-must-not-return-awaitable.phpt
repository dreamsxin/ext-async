--TEST--
Task must not return an instance of Awaitable.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
    Task::await(Task::async(function () {
        return Deferred::value(321);
    }));
} catch (\Throwable $e) {
    var_dump('Fail!');
}

?>
--EXPECT--
string(5) "Fail!"
