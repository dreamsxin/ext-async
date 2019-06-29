--TEST--
Task scheduler can deal with exit in tick callback within non-root scheduler.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

TaskScheduler::run(function() {
    $scheduler = TaskScheduler::get(TaskScheduler::class);
    
    $scheduler->tick(function () {
        var_dump('A');
    });
    
    $scheduler->tick(function () {
        exit();
    });
    
    $scheduler->tick(function () {
        var_dump('C');
    });
});

?>
--EXPECT--
string(1) "A"
