--TEST--
Task being suspended and resumed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->run(function () {
    Task::async('var_dump', ['A']);

    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        var_dump(Task::await($defer->awaitable()));
    });
    
    Task::async('var_dump', ['B']);
    
    Task::async(function () use ($defer) {
        $defer->resolve('D');
        
        var_dump('C');
    });
});

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
