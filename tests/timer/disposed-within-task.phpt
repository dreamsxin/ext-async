--TEST--
Timer can be disposed within task if unrefed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::asyncWithContext(Context::background(), function () {
    $timer = new Timer(50);

    var_dump('START');

    try {
        for ($i = 0; $i < 3; $i++) {
            $timer->awaitTimeout();
        }
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

--EXPECT--
string(5) "START"
string(32) "Task scheduler has been disposed"
