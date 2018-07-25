--TEST--
Task prevents multiple inlining of the same task instance.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/loop-scheduler.inc';

TaskScheduler::register(new TimerLoopScheduler($loop = new TimerLoop()));

Task::async(function () use (&$awaitable, $loop) {
    $defer = new Deferred();
    
    $loop->timer(50, function () use ($defer) {
        $defer->resolve(123);
    });
    
    $awaitable = Task::async(function () use ($defer) {
        return Task::await($defer->awaitable());
    });
});

Task::async(function () use (&$awaitable) {
    var_dump(Task::await($awaitable));
});

Task::async(function () use (&$awaitable) {
    var_dump(Task::await($awaitable));
});

--EXPECT--
int(123)
int(123)
