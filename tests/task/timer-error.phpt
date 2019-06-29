--TEST--
Task scheduler can deal with error thrown within timer callback.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

try {
    TaskScheduler::run(function () {
        $scheduler = TaskScheduler::get(TaskScheduler::class);
        
        $t = $scheduler->timer(function () {
            throw new \Error('FOO!');
        });
        
        $t->start(5);
    });
} catch (\Error $e) {
    var_dump($e->getMessage());
}

$scheduler = TaskScheduler::get(TaskScheduler::class);

$scheduler->tick(function () {
    var_dump('A');
});

$scheduler->timer(function () { });

$t = $scheduler->timer(function () {
    throw new \Error('FOO!');
});

$t->start(5);

$scheduler->tick(function () {
    var_dump('B');
});

?>
--EXPECTF--
string(4) "FOO!"
string(1) "A"
string(1) "B"

Fatal error: Uncaught Error: FOO! in %s:%d
Stack trace:
#0 [internal function]: Concurrent\{closure}(Object(Concurrent\TimerEvent))
#1 {main}
  thrown in %s on line %d
