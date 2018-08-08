--TEST--
Context cancellation of nested context does not affect unrelated context.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$h1 = new CancellationHandler();
$h2 = new CancellationHandler($h1->context());
$h3 = new CancellationHandler($h1->context());

Task::asyncWithContext($h2->context(), function () {
    $defer = new Deferred(function (Deferred $defer, \Throwable $e) {
        var_dump('CANCEL OP');
        
        $defer->fail($e);
    });
    
    var_dump('AWAIT DEFER');
    
    try {
        Task::await($defer->awaitable());
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

Task::asyncWithContext($h3->context(), function () {
    var_dump('START UNRELATED');
    
    (new Timer(200))->awaitTimeout();
    
    Context::current()->throwIfCancelled();
    
    var_dump(Context::current()->isCancelled());
    var_dump('DONE UNRELATED');
});

var_dump('START TIMER');

$timer = new Timer(50);
$timer->awaitTimeout();

var_dump('=> CANCEL!');
$h2->cancel();

$timer->awaitTimeout();

var_dump('DONE');

?>
--EXPECT--
string(11) "START TIMER"
string(11) "AWAIT DEFER"
string(15) "START UNRELATED"
string(10) "=> CANCEL!"
string(9) "CANCEL OP"
string(26) "Context has been cancelled"
string(4) "DONE"
bool(false)
string(14) "DONE UNRELATED"
