<?php

use Concurrent\Awaitable;
use Concurrent\Context;
use Concurrent\Task;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler();

$scheduler->task(function (): int {
    $a = new class() implements Awaitable {

        public function continueWith(callable $continuation): void
        {
            $continuation(null, 321);
        }
    };
    
    $context = Context::inherit([
        'num' => 123
    ]);
    
    $t = Task::asyncWithContext($context, function () use ($a): int {
        return min(Context::var('num'), Task::await($a));
    });
    
    return max(2 * Task::await($t), Task::await($a));
})->continueWith(function (?\Throwable $e, ?int $v = null): void {
    var_dump('CONTINUE WITH', $e, $v);
});

$scheduler->run();
