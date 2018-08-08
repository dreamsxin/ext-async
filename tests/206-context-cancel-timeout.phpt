--TEST--
Context cancellation can be triggered using an internal timer.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::asyncWithContext(Context::current()->withTimeout(100), function () {
    (new Timer(50))->awaitTimeout();

    var_dump('DONE 1');
});

Task::asyncWithContext(Context::current()->withTimeout(100), function () {
    try {
        (new Timer(200))->awaitTimeout();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }

    var_dump('DONE 2');
});

var_dump('START');

?>
--EXPECT--
string(5) "START"
string(6) "DONE 1"
string(17) "Context timed out"
string(6) "DONE 2"
