--TEST--
Context cancellation will trigger deferred cancellation callback.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$cancel = null;
$context = Context::current()->withCancel($cancel);

Task::asyncWithContext($context, function (Context $context) {
    $defer = new Deferred(function (Deferred $defer, \Throwable $e) {
        var_dump($e->getMessage());
        
        $defer->resolve(777);
    });
    
    var_dump('AWAIT DEFERRED');
    var_dump($context->isCancelled());
    
    var_dump(Task::await($defer->awaitable()));
    var_dump($context->isCancelled());
    
    try {
        $context->throwIfCancelled();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
}, $context);

var_dump('START TIMER');

$timer = new Timer(100);
$timer->awaitTimeout();

var_dump('=> CANCEL!');
$cancel();

$timer->awaitTimeout();

var_dump('DONE');

?>
--EXPECT--
string(11) "START TIMER"
string(14) "AWAIT DEFERRED"
bool(false)
string(10) "=> CANCEL!"
string(26) "Context has been cancelled"
int(777)
bool(true)
string(26) "Context has been cancelled"
string(4) "DONE"
