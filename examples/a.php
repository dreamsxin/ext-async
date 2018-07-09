<?php

use Concurrent\Deferred;
use Concurrent\Task;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler();

$scheduler->task(function (): int {
    $t = Task::async(function (): int {
        return max(123, Task::await(Deferred::value()));
    });
  
    try {
      Task::await(Deferred::error(new \Error('Fail!')));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    var_dump(2 * Task::await($t));
});

$scheduler->run();
