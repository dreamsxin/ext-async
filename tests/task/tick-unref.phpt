--TEST--
Task scheduler can deal with tick referencing.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

TaskScheduler::run(function () {
    $scheduler = TaskScheduler::get(TaskScheduler::class);
    
    $scheduler->tick(function () {
        var_dump('A');
    });
    
    $scheduler->tick(function () use ($scheduler) {
        $scheduler->tick(function () {
            var_dump('C');
        })->unref();
    
        var_dump('B');
    })->unref();
});

TaskScheduler::run(function () {
    $scheduler = TaskScheduler::get(TaskScheduler::class);
    
    $t = $scheduler->tick(function () {
        var_dump('D');
    });
    $t->unref();
    $t->unref();
    
    $t->stop();
    $t->stop();
    
    $scheduler->tick(function () use ($t) {
        $t->start();
        $t->ref();
        
        $t->start();
        $t->ref();
    
        var_dump('C');
    });
});

?>
--EXPECTF--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
