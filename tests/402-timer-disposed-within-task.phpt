--TEST--
Timer can be disposed within task if unrefed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::background(function () {
    $timer = new Timer(50);

    var_dump('START');
    var_dump(Context::isBackground());
    var_dump(Context::current()->background);

    try {
        for ($i = 0; $i < 3; $i++) {
            $timer->awaitTimeout();
        }
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

var_dump(Context::isBackground());
var_dump(Context::current()->background);

--EXPECT--
bool(false)
bool(false)
string(5) "START"
bool(true)
bool(true)
string(32) "Task scheduler has been disposed"
