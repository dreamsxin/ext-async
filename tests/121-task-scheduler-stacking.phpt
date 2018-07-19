--TEST--
Task schedulers can be stacked and unstacked.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

class TestScheduler extends LoopTaskScheduler {
    private $name;
    
    public function __construct(string $name) {
        $this->name = $name;
    }
    
    protected function activate() {
        var_dump($this->name);
    }
    
    protected function runLoop() {
        $this->dispatch();
    }
    
    protected function stopLoop() { }
}

TaskScheduler::register($s1 = new TestScheduler('S1'));

Task::await(Task::async('var_dump', 'A'));

TaskScheduler::register($s2 = new TestScheduler('S2'));

$t = Task::async('var_dump', 'B');
Task::async('var_dump', 'C');

Task::await($t);

TaskScheduler::unregister($s2);

Task::await(Task::async('var_dump', 'D'));
Task::async('var_dump', 'E');

TaskScheduler::unregister($s1);

Task::await(Task::async('var_dump', 'X'));

--EXPECT--
string(2) "S1"
string(1) "A"
string(2) "S2"
string(1) "B"
string(1) "C"
string(2) "S1"
string(1) "D"
string(2) "S1"
string(1) "E"
string(1) "X"
