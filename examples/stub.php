<?php

namespace Concurrent;

interface Awaitable
{
    public function continueWith(callable $continuation): void;
}

final class Context
{
    public function get(string $name) { }
    
    public function with(string $var, $value): Context { }
    
    public function without(string $var): Context { }
    
    public function withErrorHandler(callable $handler): Context { }
    
    public function run(callable $callback, ...$args) { }
    
    public function continueSuccess(callable $callback, $val = null): void { }
    
    public function continueError(callable $callback, \Throwable $e): void { }
    
    public function handleError(\Throwable $e): void { }
    
    public static function var(string $name) { }
    
    public static function current(): Context { }
    
    public static function inherit(?array $variables = null): Context { }
    
    public static function background(?array $variables = null): Context { }
}

final class Deferred
{
    public function __construct(?Context $context = null) { }
    
    public function awaitable(): Awaitable { }
    
    public function succeed($val = null): void { }
    
    public function fail(\Throwable $e): void { }
}

final class DeferredAwaitable implements Awaitable
{
    public function continueWith(callable $continuation): void { }
}

final class Task implements Awaitable
{
    public function continueWith(callable $continuation): void { }
    
    public static function isRunning(): bool { }
    
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
    public function __construct(?array $context = null, ?callable $errorHandler = null) { }

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
