--TEST--
Task can be executed using a custom user-defined default task scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::setDefaultScheduler(new class() extends LoopTaskScheduler
{
    protected function activate()
    {
        var_dump('ACTIVATE!');
    }
    
    protected function runLoop()
    {
        var_dump('LOOP');
        $this->dispatch();
        var_dump('DONE');
    }
    
    protected function stopLoop()
    {
        var_dump('STOP!');
    }
});

$work = function (string $v): void {
    var_dump($v);
};

var_dump('MAIN');

$t = Task::async($work, 'A');

var_dump('MAIN');
Task::await($t);

var_dump('MAIN');
Task::await(Task::async(function () use ($work) {
    $work('B');
    
    Task::async($work, 'C');
}));

var_dump('MAIN');
Task::await(Task::async(function () use ($work) {
    Task::async($work, 'E');
    Task::async($work, 'F');
    
    var_dump('D');
}));

var_dump('MAIN');

?>
--EXPECT--
string(4) "MAIN"
string(9) "ACTIVATE!"
string(4) "MAIN"
string(4) "LOOP"
string(1) "A"
string(5) "STOP!"
string(4) "DONE"
string(4) "MAIN"
string(9) "ACTIVATE!"
string(4) "LOOP"
string(1) "B"
string(5) "STOP!"
string(1) "C"
string(4) "DONE"
string(4) "MAIN"
string(9) "ACTIVATE!"
string(4) "LOOP"
string(1) "D"
string(5) "STOP!"
string(1) "E"
string(1) "F"
string(4) "DONE"
string(4) "MAIN"
string(4) "LOOP"
string(4) "DONE"
