--TEST--
Task activator is called whenever the first task is scheduled with a halted scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new class() extends TaskScheduler
{
    protected function activate()
    {
        var_dump('ACTIVATE!');
    }
};

$work = function (string $v): void {
    var_dump($v);
};

$scheduler->run($work, 'A');

$scheduler->run(function () use ($work) {
    $work('B');
    
    Task::async($work, 'C');
});

$scheduler->run(function () use ($work) {
    Task::async($work, 'D');
    Task::async($work, 'E');
});

?>
--EXPECT--
string(9) "ACTIVATE!"
string(1) "A"
string(9) "ACTIVATE!"
string(1) "B"
string(1) "C"
string(9) "ACTIVATE!"
string(1) "D"
string(1) "E"
