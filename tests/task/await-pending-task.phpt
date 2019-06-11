--TEST--
Task can await another task that is not inlined.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {
    $defer = new Deferred();

    $t = Task::async(function () use ($defer) {
        return Task::await($defer->awaitable());
    });

    $x = Task::async(function () use ($t) {
        var_dump(Task::await($t));
    });
    
    Task::async(function () use ($defer) {
        (new Timer(50))->awaitTimeout();
        
        $defer->resolve('Y');
    });
    
    var_dump('X');
    
    Task::async(function () use ($t) {
        var_dump(Task::await($t));
    });
});

var_dump('Z');

--EXPECT--
string(1) "X"
string(1) "Y"
string(1) "Y"
string(1) "Z"
