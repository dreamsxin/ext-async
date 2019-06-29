--TEST--
Task scheduler can deal with error thrown within tick callback.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

$scheduler = TaskScheduler::get(TaskScheduler::class);

$scheduler->tick(function () use ($scheduler) {
    $scheduler->tick(function () {
        var_dump('B');
    });

    var_dump('A');
});

$scheduler->tick(function () {
    throw new \Error('FOO!');
});

$scheduler->tick(function () {
    var_dump('C');
});

?>
--EXPECTF--
string(1) "A"

Fatal error: Uncaught Error: FOO! in %s:%d
Stack trace:
#0 [internal function]: Concurrent\{closure}(Object(Concurrent\TickEvent))
#1 {main}
  thrown in %s on line %d
