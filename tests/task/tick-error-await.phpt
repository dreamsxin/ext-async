--TEST--
Task scheduler can deal with tick error while root execution awaits.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

$scheduler = TaskScheduler::get(TaskScheduler::class);

$scheduler->tick(function () {
    var_dump('A');
});

$scheduler->tick(function () {
    throw new \Error('BAR!');
});

$scheduler->tick(function () {
    var_dump('B');
});

(new Timer(50))->awaitTimeout();

?>
--EXPECTF--
string(1) "A"

Fatal error: Uncaught Error: BAR! in %s:%d
Stack trace:
#0 [internal function]: Concurrent\{closure}(Object(Concurrent\TickEvent))
#1 {main}
  thrown in %s on line %d
