--TEST--
Task scheduler will activate ticks as needed.
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
    
    $t = $scheduler->tick(function () {
        var_dump('B');
    });
    
    Task::async(function () use ($t) {
        (new Timer(10))->awaitTimeout();
        
        var_dump('A');
        
        $t->start();
        
        (new Timer(10))->awaitTimeout();
        
        var_dump('C');
        $t->start();
        $t->close();
    });
    
    $t->stop();
});

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
