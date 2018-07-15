--TEST--
Task await on root level will not need a scheduler for resolved deferreds.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

var_dump(Task::await(123));

var_dump(Task::await(Deferred::value(321)));

try {
    Task::await(Deferred::error(new \Error('Fail!')));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

TaskScheduler::setDefaultScheduler(new class() extends LoopTaskScheduler {
    protected function activate() {
        var_dump('ACTIVATE');
        $this->dispatch();
    }
    
    protected function runLoop() {
        var_dump('START');
        while ($this->count()) {
            $this->dispatch();
        }
        var_dump('END');
    }
});

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
int(777)
string(5) "START"
string(3) "END"
