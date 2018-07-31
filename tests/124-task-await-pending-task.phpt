--TEST--
Task can await another task that is not inlined.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
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

    (new Timer(function () use ($defer) {
        $defer->resolve('Y');
    }))->start(50);
    
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
