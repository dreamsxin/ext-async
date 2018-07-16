--TEST--
Task scheduler (used as default) can be combiend with an event loop.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/loop-scheduler.inc';

$loop = new TimerLoop();

TaskScheduler::setDefaultScheduler(new TimerLoopScheduler($loop));

var_dump(Task::await('A'));

$defer = new Deferred();

$loop->nextTick(function () use ($defer) {
    $defer->resolve('B');
});

var_dump(Task::await($defer->awaitable()));

$defer = new Deferred();

$loop->timer(100, function () use ($defer) {
    $defer->resolve('C');
});

var_dump(Task::await($defer->awaitable()));

$defer = new Deferred();

$loop->timer(100, function () use ($defer) {
    $defer->resolve('D');
});

var_dump(Task::await($defer->awaitable()));

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
