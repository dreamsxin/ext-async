<?php

use Concurrent\Deferred;
use Concurrent\Task;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler();

$scheduler->task(function (): int {
    $t = Task::async(function (): int {
        $defer = new Deferred();
        $defer->succeed(321);
        
        return max(123, Task::await($defer->awaitable()));
    });
    
    var_dump(2 * Task::await($t));
});

$scheduler->run();
