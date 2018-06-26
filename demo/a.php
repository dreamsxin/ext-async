<?php

use Concurrent\Awaitable;
use Concurrent\Task;
use Concurrent\TaskScheduler;

$work = function (int $a, int $b): int {
    return $a + $b;
};

$continuation = function (?\Throwable $e, $v = null): void {
    var_dump('CONTINUE WITH', $e, $v);
};

$scheduler = new TaskScheduler();

$task = new Task(function () {
    $a = new class() implements Awaitable {

        public function continueWith(callable $continuation)
        {
            $continuation(null, 321);
        }
    };
    
    var_dump(Task::await($a));
    
    return 2 * Task::await($a);
});

$scheduler->start(new Task(function () use ($scheduler, $task, $continuation) {
    $scheduler->start($task);
    
    $task->continueWith($continuation);
}));

$scheduler->run();
$scheduler->run();
