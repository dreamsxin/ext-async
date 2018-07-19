<?php

namespace Concurrent;

var_dump(Fiber::backend());

$scheduler = new TaskScheduler();

$result = $scheduler->run(function () {
    $t = Task::async(function (): int {
        return max(123, Task::await(Deferred::value()));
    });
  
    var_dump([
        $t->getId(),
        $t->getFile(),
        $t->getLine()
    ]);
    
    try {
        Task::await(Deferred::error(new \Error('Fail!')));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    var_dump(2 * Task::await($t));
    
    return 777;
});

var_dump($result);
