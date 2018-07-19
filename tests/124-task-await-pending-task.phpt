--TEST--
Task can await another task that is not inlined.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/loop-scheduler.inc';

$scheduler = new TimerLoopScheduler($loop = new TimerLoop());

$scheduler->run(function () use ($loop) {
    $defer = new Deferred();

    $t = Task::async(function () use ($defer) {
        return Task::await($defer->awaitable());
    });

    $x = Task::async(function () use ($t) {
        var_dump(Task::await($t));
    });

    $loop->timer(50, function () use ($defer) {
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
