--TEST--
Task scheduler can deal with error thrown within non-root tick callback.
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
        
        $scheduler->tick(function () {
            var_dump('A');
        });
        
        $scheduler->tick(function () {
            throw new \Error('FOO!');
        });
        
        $scheduler->tick(function () {
            var_dump('C');
        });
    });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(1) "A"
string(4) "FOO!"
