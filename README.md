# Concurrent Task Extension

Provides concurrent task execution using native C fibers in PHP.

## Task API

The task extension exposes a public API that can be used to create, run and interact with fiber-based async tasks.

### Awaitable

This interface cannot be implemented directly by userland classes, implementations are provided by `Deferred` and `Task`.

```php
namespace Concurrent;

interface Awaitable { }
```

### Deferred

A deferred is a placeholder for an async operation that can be succeeded or failed from userland. It can be used to implement combinator function that operate on multiple `Awaitable` and expose a single `Awaitable` as result. The value returned from `awaitable()` is meant to be consumed by other tasks (or deferreds). The `Deferred` object itself must be kept private to the async operation because it can eighter succeed or fail the awaitable.

```php
namespace Concurrent;

final class Deferred
{
    public function awaitable(): Awaitable { }
    
    public function resolve($val = null): void { }
    
    public function fail(\Throwable $e): void { }
    
    public static function value($val = null): Awaitable { }
    
    public static function error(\Throwable $e): Awaitable { }
}
```

### Task

A task is a fiber-based object that executes a PHP function or method on a separate call stack. Tasks are created using `Task::async()` or `TaskScheduler->task()` and will not be run until `TaskScheduler->run()` is called. Calling `Task::await()` will suspend the current task if the given argument implements `Awaitable`. Passing anything else to this method will simply return the value as-is.

```php
namespace Concurrent;

final class Task implements Awaitable
{
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

The task scheduler is based on a queue of scheduled tasks that are run whenever `dispatch()` is called. The scheduler will start (or resume) all tasks that are scheduled for execution and return when no more tasks are scheduled. Tasks may be re-scheduled (an hence run multiple times) during a single call to the dispatch method. The scheduler implements `Countable` and will return the current number of scheduled tasks.

You can extend the `TaskScheduler` class to create a scheduler with support for an event loop. The scheduler provides integration by letting you override the `runLoop()` method that should start the event loop and keep it running until no more events can occur. The primary problem with event loop integration is that you need to call `dispatch()` whenever tasks are ready run. You can override the `activate()` method to schedule execution of the `dispatch()` with your event loop (future tick or defer watcher). The scheduler will call `activate` whenever a task is registered for execution and the scheduler is not in the process of dispatching tasks.

```php
namespace Concurrent;

class TaskScheduler implements \Countable
{
    public final function count(): int { }
    
    public final function run(callable $callback, ?array $args = null): mixed { }
    
    public final function runWithContext(Context $context, callable $callback, ?array $args = null): mixed { }
    
    protected final function dispatch(): void { }
    
    protected function activate(): void { }
    
    protected function runLoop(): void { }
}
```

### Context

Each task runs in a `Context` that provides access to task-local variables. These variables are are also available to every `Task` re-using the same context or an inherited context. An implicit root context is always available, therefore it is always possible to access the current context or inherit from it. You can access a contextual value by calling `Context::var()` which will lookup the value in the current active context. The lookup call will return `null` when the value is not set in the active context or no context is active during the method call.

You need to inherit a new context whenever you want to set task-local variables. In order for your new context to be used you need have to pass it to a task using `Task::asyncWithContext()` or you can enable it for the duration of a function / method call by calling `run()`. The later is preferred if your code is executing in a single task and you just want to add some variables.

```php
namespace Concurrent;

final class Context
{
    public function get(string $name): mixed { }
    
    public function with(string $var, $value): Context { }
    
    public function without(string $var): Context { }

    public function run(callable $callback, ...$args): mixed { }
    
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

### PHP with support for async & await keywords

You can install a patched version of PHP that provides native support for `async` and `await` as described in the transformation section. To get up and running with it you can execute this in your shell:

```shell
mkdir php-src
curl -LSs https://github.com/concurrent-php/php-src/archive/async.tar.gz | sudo tar -xz -C "php-src" --strip-components 1

pushd php-src
./buildconf --force
./configure --prefix=/usr/local/php/cli --with-config-file-path=/usr/local/php/cli --without-pear
make -j4
make install
popd

mkdir task
curl -LSs https://github.com/concurrent-php/task/archive/master.tar.gz | sudo tar -xz -C "task" --strip-components 1

pushd task
phpize
./configure
make install
popd
```

This will install a modified version of PHP's master branch that has full support for `async` and `await`. It will also install the `task` extension that is required for the actual async execution model.

### Source Transformation Examples

The source transformation needs to consider namespaces and preserve scope. Dealing with namespaces is a problem when it comes to function calls because there is a fallback to the global namespace involved and there is no way to determine the called function in all cases during compilation. Here are some examples of code using `async` / `await` syntax and the transformed source code:

```php
$task = async max(1, 2, 3);
$result = await $task;

$task = \Concurrent\Task::async('max', [1, 2, 3]);
$result = \Concurrent\Task::await($task);
``` 
Function calls in global namespace only have to check imported functions, the correct function can be determined at compile time.

```php
namespace Foo;

$task = async bar(1, 2);

$task = \Concurrent\Task::async(\function_exists('Foo\\bar') ? 'Foo\\bar' : 'bar', [1, 2]);
```
Unqualified function calls in namespace require runtime evaluation of the function to be called (unless the function is imported via use statement).

```php
namespace Foo;

$context = \Concurrent\Context::inherit(['num' => 321]);
$work = function (int $a): int { return $a + Context::var('num'); };

$result = await async $context => $work(42);

$result = \Concurrent\Task::await(\Concurrent\Task::asyncWithContext(\Closure::fromCallable($work), [42]));
```
Calling functions stored in variables requires to keep track of the calling scope because `$work` might contain a method call (or an object with `__invoke()` method) with a visibility other than `public`.
