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
    public function continueWith(callable $continuation): void;
}

final class Task
{
    public function __construct(TaskScheduler $scheduler, callable $callback, ?array $args = null) { }
    
    public static function async(callable $callback, ?array $args = null): Task { }
    
    public static function await($a) { }
    
    public function continueWith(callable $continuation): void { }
}

final class TaskScheduler
{
    public function task(callable $callback, ?array $args = null): Task { }
    
    public function run(): void { }
}
