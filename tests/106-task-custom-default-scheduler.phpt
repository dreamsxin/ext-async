--TEST--
Task can be executed using a custom user-defined default task scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::setDefaultScheduler(new class() extends TaskScheduler
{
    protected function activate()
    {
        var_dump('ACTIVATE!');
    }
});

$work = function (string $v): void {
    var_dump($v);
};

Task::await(Task::async($work, 'A'));

Task::await(Task::async(function () use ($work) {
    $work('B');
    
    Task::async($work, 'C');
}));

Task::await(Task::async(function () use ($work) {
    Task::async($work, 'D');
    Task::async($work, 'E');
}));

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
