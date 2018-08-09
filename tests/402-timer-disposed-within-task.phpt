--TEST--
Timer can be disposed within task if unrefed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::asyncWithContext(Context::current()->background(), function () {
    $timer = new Timer(50);

    var_dump('START');
    var_dump(Context::current()->isBackground());

    try {
        for ($i = 0; $i < 3; $i++) {
            $timer->awaitTimeout();
        }
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

var_dump(Context::current()->isBackground());

--EXPECT--
bool(false)
string(5) "START"
bool(true)
string(23) "Task has been destroyed"
