--TEST--
Timeout can be unreferenced.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::asyncWithContext(Context::background(), function () {
    try {
        Task::await(Timer::timeout(2000));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

--EXPECT--
string(32) "Task scheduler has been disposed"
