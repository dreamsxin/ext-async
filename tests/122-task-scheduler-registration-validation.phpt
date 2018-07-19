--TEST--
Task schedulers cannot be registered while they are not active.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
    TaskScheduler::unregister(new TaskScheduler());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

TaskScheduler::register(new TaskScheduler());

try {
    TaskScheduler::unregister(new TaskScheduler());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(71) "Cannot unregister task scheduler because it is not the active scheduler"
string(71) "Cannot unregister task scheduler because it is not the active scheduler"
