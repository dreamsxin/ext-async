<?php

/*
 +----------------------------------------------------------------------+
 | PHP Version 7                                                        |
 +----------------------------------------------------------------------+
 | Copyright (c) 1997-2018 The PHP Group                                |
 +----------------------------------------------------------------------+
 | This source file is subject to version 3.01 of the PHP license,      |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.php.net/license/3_01.txt                                  |
 | If you did not receive a copy of the PHP license and are unable to   |
 | obtain it through the world-wide-web, please send a note to          |
 | license@php.net so we can mail you a copy immediately.               |
 +----------------------------------------------------------------------+
 | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
 +----------------------------------------------------------------------+
 */

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
    
    public static function combine(array $awaitables, callable $continuation): Awaitable { }
}

final class Task implements Awaitable
{
    public static function isRunning(): bool { }
    
    public static function async(callable $callback, ...$args): Task { }
    
    public static function asyncWithContext(Context $context, callable $callback, ...$args): Task { }
    
    /**
     * @throws \Throwable Depends on the awaited operation.
     */
    public static function await($a) { }
}

class TaskScheduler implements \Countable
{
    public final function count(): int { }
    
    public final function run(callable $callback, ...$args) { }
    
    public final function runWithContext(Context $context, callable $callback, ...$args) { }
    
    protected final function dispatch(): void { }
    
    public static final function setDefaultScheduler(TaskScheduler $scheduler): void { }
}

abstract class LoopTaskScheduler extends TaskScheduler
{   
    protected function activate(): void { }
    
    protected function runLoop(): void { }
    
    protected function stopLoop(): void { }
}

final class Fiber
{
    public function __construct(callable $callback, ?int $stack_size = null) { }
    
    public function status(): int { }
    
    public function start(...$args) { }
    
    public function resume($val = null) { }
    
    public function throw(\Throwable $e) { }
    
    public static function isRunning(): bool { }
    
    public static function backend(): string { }
    
    public static function yield($val = null) { }
}
