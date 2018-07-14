--TEST--
Task can be executed using a custom user-defined default task scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::setDefaultScheduler(new class() extends TaskLoopScheduler
{
    protected function activate()
    {
        var_dump('ACTIVATE!');
    }
    
    protected function runLoop()
    {
        while ($this->count()) {
            $this->dispatch();
        }
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
string(1) "A"
string(4) "MAIN"
string(1) "B"
string(4) "MAIN"
string(1) "C"
string(1) "D"
string(4) "MAIN"
string(1) "E"
string(1) "F"
