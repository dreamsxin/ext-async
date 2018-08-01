<?php

namespace Concurrent;

var_dump(Fiber::backend());

$result = TaskScheduler::run(function () {
    $t = Task::async(function (): int {
        return max(123, Task::await(Deferred::value()));
    });
    
    print_r($t);
    
    try {
        Task::await(Deferred::error(new \Error('Fail!')));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    var_dump(2 * Task::await($t));
    
    return 777;
}, function (TaskScheduler $scheduler) {
    print_r($scheduler->getPendingTasks());
});

$timer = new Timer(500);
$timer->awaitTimeout();
var_dump($result);
