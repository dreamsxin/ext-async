# Concurrent Task Extension

Provides concurrent task execution using native C fibers in PHP.

## Task API

The task extension exposes a public API that can be used to create, run and interact with fiber-based async tasks.

### Awaitable

Interface to be implemented by userland classes that want to provide some async operation that can be awaited by tasks. The `$continuation` callback takes a `Throwable` as first argument when the operation has failed (second argument must be optional), successful operations pass `null` as first argument and the result as second argument.

```php
namespace Concurrent;

interface Awaitable
{
    public function continueWith(callable<?Throwable, mixed>: void $continuation): void;
}
```

### Task

A task is a fiber-based object that executes a PHP function or method on a separate call stack. Tasks are created using `Task::async()` or `TaskScheduler->task()` and will not be run until `TaskScheduler->run()` is called. Calling `Task::await()` will suspend the current task if the given argument implements `Awaitable`. Passing anything else to this method will simply return the value as-is (with the exception of values that are transformed by an adapter registered with the scheduler).

```php
final class Task implements Awaitable
{
    public function continueWith(callable $continuation): void { }
    
    public static function async(callable $callback, ?array $args = null): Task { }
    
    public static function await($a) { }
}
```

### TaskScheduler

The task scheduler has a queue of scheduled tasks that are run whenever `TaskScheduler->run()` is called. The scheduler will start (or resume) all tasks that are scheduled for execution and return when no more tasks are scheduled. Tasks may be re-scheduled (an hence run multiple times) during a single call to the run method. The scheduler implements `Countable` and will return the current number of scheduled tasks.

You can set an `activator` callback, that will be called whenever the scheduler is not running and the first task is scheduled for execution. This allows for easy integration of the task scheduler with event loops as you can register the run method as future tick (react) or defer watcher (amp).

The `adapter` callback is called whenever an object that does not implement `Awaitable` is passed to `Task::await()`. This provides an easy way to adapt promises (react, amp, ...) to the `Awaitable` interface using adapter classes.

```php
final class TaskScheduler implements \Countable
{
    public function count(): int { }
    
    public function task(callable $callback, ?array $args = null): Task { }
    
    public function activator(callable<TaskScheduler>: void $callback): void { }
    
    public function adapter(callable<mixed>: mixed $callback): void { }
    
    public function run(): void { }
}
```

### Fiber

A lower-level API for concurrent callback execution is available through the `Fiber` API. The underlying stack-switching is the same as in the `Task` implementation but fibers do not come with a scheduler or a higher level abstraction of continuations. A fiber must be started and resumed by the caller in PHP userland. Calling `Fiber::yield()` will suspend the fiber and return the yielded value to `start()`, `resume()` or `throw()`. The `status()` method is needed to check if the fiber has been run to completion.

```php
final class Fiber
{
    public function __construct(callable $callback, ?int $stack_size = null) { }
    
    public function status(): int { }
    
    public function start(...$args) { }
    
    public function resume($val = null) { }
    
    public function throw(\Throwable $e) { }
    
    public static function yield($val = null) { }
}
```

## Async / Await Keyword Transformation

The extension provide `Task::async()` and `Task::await()` static methods that are implemented in a way that allows for a very simple transformation to the keyword `async` and `await` which could be introduced into PHP some time in the future.

```php
$task = async $this->sendRequest($request, $timeout);
$response = await $task;

// This would be equivalent to the following:
$task = Task::async(Closure::fromCallable($this, 'sendRequest'), [$request, $timeout]);
$response = Task::await($task);
```

The example shows a possible syntax for a keyword-based async execution model. The `async` keyword can be prepended to any function or method call to create a `Task` object instead of executing the call directly. The calling scope should be preserved by this operation, hence being consistent with the way method calls work in PHP (no need to create a closure in userland code). The `await` keyword is equivalent to calling `Task::await()` but does not require a function call, it can be implemented as an opcode handler in the Zend VM.
