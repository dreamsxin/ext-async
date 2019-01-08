--TEST--
Context cancellation is chained with parent cancellation.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$h1 = null;
$c1 = Context::current()->withCancel($h1);

$h2 = null;
$c2 = $c1->withCancel($h2);

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

var_dump('START TIMER');

$timer = new Timer(50);
$timer->awaitTimeout();

var_dump('=> CANCEL!');
$h1();

$timer->awaitTimeout();

var_dump('DONE');

?>
--EXPECT--
string(11) "START TIMER"
string(11) "AWAIT DEFER"
string(10) "=> CANCEL!"
string(9) "CANCEL OP"
string(26) "Context has been cancelled"
string(4) "DONE"
