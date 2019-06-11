--TEST--
Context cancellation of nested context does not affect unrelated context.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$h1 = null;
$c1 = Context::current()->withCancel($h1);

$h2 = null;
$c2 = $c1->withCancel($h2);

$h3 = null;
$c3 = $c1->withCancel($h3);

Task::asyncWithContext($c2, function () {
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

Task::asyncWithContext($c3, function () {
    var_dump('START UNRELATED');
    
    (new Timer(300))->awaitTimeout();
    
    $context = Context::current();
    
    $context->throwIfCancelled();
    
    var_dump($context->isCancelled());
    var_dump('DONE UNRELATED');
});

var_dump('START TIMER');

$timer = new Timer(50);
$timer->awaitTimeout();

var_dump('=> CANCEL!');
$h2();

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
