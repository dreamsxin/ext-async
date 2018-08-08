--TEST--
Context can be shielded from cancellation.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$handler = new CancellationHandler();

Task::asyncWithContext($handler->context(), function () {
    try {
        (new Timer(50))->awaitTimeout();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }

    var_dump('DONE 1');
});

Task::asyncWithContext($handler->context()->shield(), function () {
    (new Timer(50))->awaitTimeout();

    var_dump('DONE 2');
});

var_dump('START');

(new Timer(10))->awaitTimeout();

$handler->cancel();

?>
--EXPECT--
string(5) "START"
string(26) "Context has been cancelled"
string(6) "DONE 1"
string(6) "DONE 2"
