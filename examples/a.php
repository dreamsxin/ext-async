<?php

use Concurrent\Awaitable;
use Concurrent\Context;
use Concurrent\Task;
use Concurrent\TaskScheduler;

class Dummy implements Awaitable
{
    private $key;
    
    private $context;

    public function __construct(string $key, ?Context $context = null)
    {
        $this->key = $key;
        $this->context = $context ?? Context::current();
    }

    public function continueWith(callable $continuation): void
    {
        try {
            $this->context->run($continuation, null, $this->context->get($this->key));
        } catch (\Throwable $e) {
            $this->context->handleError($e);
        }
    }
}

$scheduler = new TaskScheduler();

$scheduler->task(function (): int {
    $context = Context::inherit([
        'result' => 321
    ]);
    
    $t = Task::asyncWithContext($context, function (): int {
        return max(123, Task::await(new Dummy('result')));
    });
    
    return 2 * Task::await($t);
})->continueWith(function (?\Throwable $e, ?int $v = null): void {
    var_dump('CONTINUE WITH', $e, $v);
});

$scheduler->run();
