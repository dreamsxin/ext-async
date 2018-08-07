--TEST--
Context cancellation will spread to internally suspended operation.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$handler = new CancellationHandler(Context::current());

Task::asyncWithContext($handler->context(), function () {
    $signal = new SignalWatcher(SignalWatcher::SIGINT);
    
    var_dump('AWAIT SIGNAL');
    
    try {
        $signal->awaitSignal();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    try {
        $signal->awaitSignal();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    var_dump('DONE TASK');
});

var_dump('START TIMER');

$timer = new Timer(25);
$timer->awaitTimeout();

var_dump('=> CANCEL');
$handler->cancel();

$timer->awaitTimeout();

var_dump('DONE');

?>
--EXPECT--
string(11) "START TIMER"
string(12) "AWAIT SIGNAL"
string(9) "=> CANCEL"
string(26) "Context has been cancelled"
string(26) "Context has been cancelled"
string(9) "DONE TASK"
string(4) "DONE"
