--TEST--
Task schedulers cannot be popped while they are not active.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
    TaskScheduler::pop(new TaskScheduler());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

TaskScheduler::push(new TaskScheduler());

try {
    TaskScheduler::pop(new TaskScheduler());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(64) "Cannot pop task scheduler because it is not the active scheduler"
string(64) "Cannot pop task scheduler because it is not the active scheduler"
