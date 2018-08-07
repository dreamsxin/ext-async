--TEST--
Context cancellation will trigger deferred cancellation callback.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$handler = new CancellationHandler();

Task::asyncWithContext($handler->context(), function () {
    $defer = new Deferred(function (Deferred $defer, \Throwable $e) {
        var_dump($e->getMessage());
        
        $defer->resolve(777);
    });
    
    var_dump('AWAIT DEFERRED');
    var_dump(Task::await($defer->awaitable()));
});

var_dump('START TIMER');

$timer = new Timer(100);
$timer->awaitTimeout();

var_dump('=> CANCEL!');
$handler->cancel();

$timer->awaitTimeout();

var_dump('DONE');

?>
--EXPECT--
string(11) "START TIMER"
string(14) "AWAIT DEFERRED"
string(10) "=> CANCEL!"
string(26) "Context has been cancelled"
int(777)
string(4) "DONE"
