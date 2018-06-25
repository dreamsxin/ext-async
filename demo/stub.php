<?php

namespace Concurrent;

final class Fiber
{
    public function __construct(callable $callback, ?int $stack_size = null) { }

    public function status(): int { }

    public function start(...$args) { }

    public function resume($val = null) { }

    public function throw(\Throwable $e) { }

    public static function yield($val = null) { }
}

interface Awaitable
{
    public function continueWith(callable $continuation);
}

class Task implements Awaitable
{
    public function continueWith(callable $continuation) { }
    
    public static function await($a) { }
}

class TaskScheduler
{
    public function start(Task $task): void { }
    
    public function schedule(Task $task, $val = null): void { }
    
    public function scheduleError(Task $task, \Throwable $e): void { }
    
    public function run(): void { }
}
