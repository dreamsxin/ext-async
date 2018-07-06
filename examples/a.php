<?php

use Concurrent\Deferred;
use Concurrent\Task;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler();

$scheduler->task(function (): int {
    $t = Task::async(function (): int {
        $defer = new Deferred();
        
        $defer->awaitable()->continueWith(function (?\Throwable $e, $v = null) {
            var_dump('DEFERRED', $e, $v);
        });
        
        $defer->succeed(321);
        
        return max(123, Task::await($defer->awaitable()));
    });
    
    return 2 * Task::await($t);
})->continueWith(function (?\Throwable $e, ?int $v = null): void {
    var_dump('CONTINUE WITH', $e, $v);
});

$scheduler->run();
