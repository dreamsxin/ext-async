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
    public function continueWith(callable(?\Throwable, mixed = null) $continuation): void;
}
```

Neighter continuation callbacks nor `continueWith()` must throw an error. If an error is thrown it must be handled using the `Context` API. This requires every implementation of `Awaitable` to keep a reference to a `Context` (usually set when the awaitable is created).

```php
namespace Concurrent;

class Example implements Awaitable
{
    private $context;
    
    public function __construct(?Context $context = null)
    {
        $this->context = $context ?? Context::current();
    }

    public function continueWith(callable $continuation): void
    {
        $this->context->continueSuccess($continuation, 'DONE');
    }
}
```

### Task

A task is a fiber-based object that executes a PHP function or method on a separate call stack. Tasks are created using `Task::async()` or `TaskScheduler->task()` and will not be run until `TaskScheduler->run()` is called. Calling `Task::await()` will suspend the current task if the given argument implements `Awaitable`. Passing anything else to this method will simply return the value as-is (with the exception of values that are transformed by an adapter registered with the scheduler).

```php
namespace Concurrent;

final class Task implements Awaitable
{
    public function continueWith(callable(?\Throwable, mixed = null) $continuation): void { }
    
    public static function isRunning(): bool { }
    
    /* Should be replaced with async keyword if merged into PHP core. */
    public static function async(callable $callback, ?array $args = null): Task { }
    
    /* Should be replaced with extended async keyword expression if merged into PHP core. */
    public static function asyncWithContext(Context $context, callable $callback, ?array $args = null): Task { }
    
    /* Should be replaced with await keyword if merged into PHP core. */
    public static function await($a): mixed { }
}
```

### TaskScheduler

The task scheduler is based on a queue of scheduled tasks that are run whenever `TaskScheduler->run()` is called. The scheduler will start (or resume) all tasks that are scheduled for execution and return when no more tasks are scheduled. Tasks may be re-scheduled (an hence run multiple times) during a single call to the run method. The scheduler implements `Countable` and will return the current number of scheduled tasks.

The constructor takes an associative array of context variables that will be used by the root `Context`. Each task being run by the scheduler will be bound to the root context by default.

You can set an `activator` callback, that will be called whenever the scheduler is not running and the first task is scheduled for execution. This allows for easy integration of the task scheduler with event loops as you can register the run method as future tick (react) or defer watcher (amp).

The `adapter` callback is called whenever an object that does not implement `Awaitable` is passed to `Task::await()`. This provides an easy way to adapt promises (react, amp, ...) to the `Awaitable` interface using adapter classes.

```php
namespace Concurrent;

final class TaskScheduler implements \Countable
{
    public function __construct(?array $context = null, ?callable(\Throwable) $errorHandler = null) { }

    public function count(): int { }
    
    public function task(callable $callback, ?array $args = null): Task { }
    
    public function run(): void { }
    
    /* Used for event loop integration, should be dropped if merged into PHP core. */
    public function activator(callable(TaskScheduler) $callback): void { }
    
    /* Used for promise-API bridging, should be dropped if merged into PHP core. */
    public function adapter(callable(mixed) $callback): void { }
}
```

### Context

Each task runs in a `Context` that provides access to task-local variables. These variables are are also available to every `Task` re-using the same context or an inherited context. The `TaskScheduler` creates an implicit root context that every created task will use by default. You can access a contextual value by calling `Context::var()` which will lookup the value in the current active context. The lookup call will return `null` when the value is not set in the active context or no context is active during the method call.

You need to inherit a new context whenever you want to set task-local variables. In order for your new context to be used you need have to pass it to a task using `Task::asyncWithContext()` or you can enable it for the duration of a function / method call by calling `run()`. The later is preferred if your code is executing in a single task and you just want to add some variables.

```php
namespace Concurrent;

final class Context
{
    public function get(string $name) { }
    
    public function with(string $var, $value): Context { }
    
    public function without(string $var): Context { }

    public function withErrorHandler(callable $handler): Context { }
    
    public function run(callable $callback, ...$args): mixed { }
    
    public function continueSuccess(callable $callback, $val = null): void { }
    
    public function continueError(callable $callback, \Throwable $e): void { }
    
    public function handleError(\Throwable $e): void { }
    
    public static function var(string $name): mixed { }
    
    public static function current(): Context { }
    
    public static function inherit(?array $variables = null): Context { }
    
    public static function background(?array $variables = null): Context { }
}
```

### Fiber

A lower-level API for concurrent callback execution is available through the `Fiber` API. The underlying stack-switching is the same as in the `Task` implementation but fibers do not come with a scheduler or a higher level abstraction of continuations. A fiber must be started and resumed by the caller in PHP userland. Calling `Fiber::yield()` will suspend the fiber and return the yielded value to `start()`, `resume()` or `throw()`. The `status()` method is needed to check if the fiber has been run to completion yet.

```php
namespace Concurrent;

final class Fiber
{
    public function __construct(callable $callback, ?int $stack_size = null) { }
    
    public function status(): int { }
    
    public function start(...$args): mixed { }
    
    public function resume($val = null): mixed { }
    
    public function throw(\Throwable $e): mixed { }
    
    public static function isRunning(): bool { }
    
    public static function yield($val = null): mixed { }
}
```

## Async / Await Keyword Transformation

The extension provides `Task::async()` and `Task::await()` static methods that are implemented in a way that allows for a very simple transformation to the keywords `async` and `await` which could be introduced into PHP some time in the future.

```php
$task = async $this->sendRequest($request, $timeout);
$response = await $task;

// The above code would be equivalent to the following:
$task = Task::async(Closure::fromCallable($this, 'sendRequest'), [$request, $timeout]);
$response = Task::await($task);
```

The example shows a possible syntax for a keyword-based async execution model. The `async` keyword can be prepended to any function or method call to create a `Task` object instead of executing the call directly. The calling scope should be preserved by this operation, hence being consistent with the way method calls work in PHP (no need to create a closure in userland code). The `await` keyword is equivalent to calling `Task::await()` but does not require a function call, it can be implemented as an opcode handler in the Zend VM.

```php
$context = Context::inherit(['foo' => 'bar']);

$task = async $context => doSomething($a, $b);
$result = await $task;

// The above code would be equivalent to the following:
$task = Task::asyncWithContext($context, 'doSomething', [$a, $b]);
$result = Task::await($task);
```

The second example shows how passing a new context to a task would also be possible using the `async` keyword. This would allow for a very simple and readable way to setup tasks in a specific context using a keyword-based syntax.
