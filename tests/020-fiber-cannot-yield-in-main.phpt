--TEST--
Fiber yield can only be used while a fiber is running.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;
use Concurrent\Task;

try {
    Fiber::yield();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async(function () {
        Fiber::yield();
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECTF--
string(33) "Cannot yield from outside a fiber"
string(31) "Cannot yield from an async task"
