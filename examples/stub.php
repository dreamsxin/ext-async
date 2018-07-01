<?php

namespace Concurrent;

interface Awaitable
{
    public function continueWith(callable $continuation): void;
}

class Context
{
    public function get(string $name) { }

    public static function lookup(string $name) { }

    public static function current(): Context { }

    public static function inherit(array $variables): Context { }
}

final class Task implements Awaitable
{
    public function continueWith(callable $continuation): void { }
    
    public static function isRunning(): bool;
    
    public static function async(callable $callback, ?array $args = null): Task { }
    
    public static function asyncWithContext(Context $context, callable $callback, ?array $args = null): Task { }
    
    public static function await($a) { }
}

final class TaskContinuation
{
    public function __invoke(?\Throwable $e, $v = null) { }
}

final class TaskScheduler implements \Countable
{
    public function __construct(?array $context = null) { }

    public function count(): int { }
    
    public function task(callable $callback, ?array $args = null): Task { }
    
    public function run(): void { }
    
    public function activator(callable $callback): void { }
    
    public function adapter(callable $callback): void { }
}

final class Fiber
{
    public function __construct(callable $callback, ?int $stack_size = null) { }
    
    public function status(): int { }
    
    public function start(...$args) { }
    
    public function resume($val = null) { }
    
    public function throw(\Throwable $e) { }
    
    public static function isRunning(): bool;
    
    public static function yield($val = null) { }
}
