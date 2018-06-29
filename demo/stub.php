<?php

namespace Concurrent;

interface Awaitable
{
    public function continueWith(callable $continuation): void;
}

final class Task implements Awaitable
{
    public function __construct(TaskScheduler $scheduler, callable $callback, ?array $args = null) { }
    
    public function continueWith(callable $continuation): void { }
    
    public static function async(callable $callback, ?array $args = null): Task { }
    
    public static function await($a) { }
}

final class TaskContinuation
{
    public function __invoke(?\Throwable $e, $v = null) { }
}

final class TaskScheduler implements \Countable
{
    public function count(): int { }
    
    public function task(callable $callback, ?array $args = null): Task { }
    
    public function activator(callable $callback): void { }
    
    public function adapter(callable $callback): void { }
    
    public function run(): void { }
}

final class Fiber
{
    public function __construct(callable $callback, ?int $stack_size = null) { }
    
    public function status(): int { }
    
    public function start(...$args) { }
    
    public function resume($val = null) { }
    
    public function throw(\Throwable $e) { }
    
    public static function yield($val = null) { }
}
