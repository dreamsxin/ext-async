<?php

namespace Concurrent;

interface Awaitable { }

final class Context
{
    public function get(string $name) { }
    
    public function with(string $var, $value): Context { }
    
    public function without(string $var): Context { }
    
    public function run(callable $callback, ...$args) { }
    
    public static function var(string $name) { }
    
    public static function current(): Context { }
    
    public static function inherit(?array $variables = null): Context { }
    
    public static function background(?array $variables = null): Context { }
}

final class Deferred
{
    public function awaitable(): Awaitable { }
    
    public function resolve($val = null): void { }
    
    public function fail(\Throwable $e): void { }
    
    public static function value($val = null): Awaitable { }
    
    public static function error(\Throwable $e): Awaitable { }
}

final class DeferredAwaitable implements Awaitable { }

final class Task implements Awaitable
{
    public static function isRunning(): bool { }
    
    public static function async(callable $callback, ?array $args = null): Task { }
    
    public static function asyncWithContext(Context $context, callable $callback, ?array $args = null): Task { }
    
    public static function await($a) { }
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
    
    public static function isRunning(): bool { }
    
    public static function yield($val = null) { }
}
