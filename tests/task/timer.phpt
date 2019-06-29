--TEST--
Task scheduler can scheduler callback timers.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

var_dump('A');

TaskScheduler::run(function () {
    $scheduler = TaskScheduler::get(TaskScheduler::class);
    
    $scheduler->timer(function (TimerEvent $t) {
        static $i = 0;
    
        if ($i++ > 2) {
            $t->stop();
        } else {
            var_dump('X');
        }
    })->start(10, true);
});

var_dump('B');

TaskScheduler::run(function () {
    $scheduler = TaskScheduler::get(TaskScheduler::class);
    
    $x = $scheduler->timer(function (TimerEvent $t) {
        var_dump('TIMER');
        $t->start(100);
    });
    $x->start(10);
    
    $t = $scheduler->timer(function (TimerEvent $t) use ($x) {
        var_dump('UNREF');
        $t->unref();
        $x->close();
    });
    $t->start(40, true);
    $t->ref();
});

var_dump('C');

TaskScheduler::run(function () {
    TaskScheduler::get(TaskScheduler::class)->timer(function () {
        throw new \Error('NEVER!');
    });
});

var_dump('D');

?>
--EXPECT--
string(1) "A"
string(1) "X"
string(1) "X"
string(1) "X"
string(1) "B"
string(5) "TIMER"
string(5) "UNREF"
string(1) "C"
string(1) "D"
