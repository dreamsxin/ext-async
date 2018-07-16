--TEST--
Task activator is called whenever the first task is scheduled with a halted scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new class() extends LoopTaskScheduler
{
    protected function activate()
    {
        var_dump('ACTIVATE!');
    }
    
    protected function runLoop()
    {
        var_dump('RUN LOOP!');
        
        while ($this->count()) {
            $this->dispatch();
        }
        
        var_dump('END LOOP!');
    }
    
    protected function stopLoop()
    {
        var_dump('STOP LOOP!');
    }
};

$work = function (string $v): void {
    var_dump($v);
};

var_dump('MAIN');

$scheduler->run($work, 'A');

var_dump('MAIN');

$scheduler->run(function () use ($work) {
    $work('B');
    
    Task::async($work, 'C');
});

var_dump('MAIN');

$scheduler->run(function () use ($work) {
    Task::async($work, 'D');
    Task::async($work, 'E');
});

var_dump('MAIN');

?>
--EXPECT--
string(4) "MAIN"
string(9) "ACTIVATE!"
string(9) "RUN LOOP!"
string(1) "A"
string(9) "END LOOP!"
string(4) "MAIN"
string(9) "ACTIVATE!"
string(9) "RUN LOOP!"
string(1) "B"
string(1) "C"
string(9) "END LOOP!"
string(4) "MAIN"
string(9) "ACTIVATE!"
string(9) "RUN LOOP!"
string(1) "D"
string(1) "E"
string(9) "END LOOP!"
string(4) "MAIN"
