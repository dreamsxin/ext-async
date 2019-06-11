--TEST--
Timer can be closed while being awaited in a task.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$timer = new Timer(50);

Task::async(function () use ($timer) {
    $timer->close(new \Error('FAIL!'));
});

var_dump('START');

try {
    $timer->awaitTimeout();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

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
