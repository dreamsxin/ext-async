--TEST--
Task awaiting arbitrary values.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->task(function () {
    var_dump('A');
    
    var_dump(Task::await('B'));
    
    $defer = new Deferred();
    $defer->resolve('C');
    
    var_dump(Task::await($defer->awaitable()));
    
    var_dump(Task::await(Task::async(function (string $x): string {
        return $x;
    }, ['D'])));
    
    Task::await($t = Task::async(function () {
        return 'E';
    }));
    
    var_dump(Task::await($t));
});

$scheduler->run();

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
string(1) "E"
