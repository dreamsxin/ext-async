--TEST--
Timer can be closed while being awaited in a task.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$timer = new Timer(50);

$t = Task::async(function () use ($timer) {
    var_dump('START');

    try {
        for ($i = 0; $i < 3; $i++) {
            $timer->awaitTimeout();
            
            var_dump($i);
        }
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
        var_dump($e->getPrevious()->getMessage());
    }
});

Task::async(function () use ($timer) {
    (new Timer(10))->awaitTimeout();
    
    $timer->close(new \Error('FAIL!'));
});

Task::await($t);

try {
    $timer->awaitTimeout();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}
    
--EXPECT--
string(5) "START"
string(21) "Timer has been closed"
string(5) "FAIL!"
string(21) "Timer has been closed"
string(5) "FAIL!"
