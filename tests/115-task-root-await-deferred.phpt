--TEST--
Task await on root level will not need a scheduler for resolved deferreds.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(new class() extends LoopTaskScheduler {
    protected function activate() {
        var_dump('ACTIVATE');
    }
    
    protected function runLoop() {
        var_dump('START');
        $this->dispatch();
        var_dump('END');
    }
    
    protected function stopLoop() {
        var_dump('STOP');
    }
});

var_dump(Task::await(123));

var_dump(Task::await(Deferred::value(321)));

try {
    Task::await(Deferred::error(new \Error('Fail!')));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

$defer = new Deferred();

Task::async(function () use ($defer) {
    $defer->resolve(777);
});

var_dump(Task::await($defer->awaitable()));

?>
--EXPECT--
int(123)
int(321)
string(5) "Fail!"
string(8) "ACTIVATE"
string(5) "START"
string(4) "STOP"
string(3) "END"
int(777)
string(5) "START"
string(3) "END"
