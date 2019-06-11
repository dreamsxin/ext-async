# Async Extension for PHP

[![Build Status](https://travis-ci.org/concurrent-php/ext-async.svg?branch=master)](https://travis-ci.org/concurrent-php/ext-async)
[![Coverage Status](https://coveralls.io/repos/github/concurrent-php/ext-async/badge.svg)](https://coveralls.io/github/concurrent-php/ext-async)
[![Minimum PHP Version](https://img.shields.io/badge/PHP-7.3-8892BF.svg)](https://github.com/concurrent-php/ext-async)

Provides concurrent Zend VM executions using native C fibers in PHP.

## Installation

The `async` extension is not published as `pecl` extension (yet).

### Linux / MacOS

Installing `ext-async` from source works like this (assuming you have PHP 7.3 installed):
```shell
mkdir ext-async
curl -LSs https://github.com/concurrent-php/ext-async/archive/master.tar.gz | tar -xz -C "ext-async" --strip-components 1

pushd ext-async
phpize
./configure
make install
popd
```

These are the options supported by `configure`:

| Option | Description |
| --- | --- |
| **--with-openssl[=DIR]** | Allows you to specify the directory where `libssl-dev` is installed. |
| **--with-valgrind[=DIR]** | Can be used to enable Valgrind support and (optional) specify the valgrind directory. |

You can use `make` to run all test cases (or test from a specific directory only):
```shell
make test
make test TESTS=test/{DIR}/
```

> Warning: If you want to compile the extension on different machines (local, docker, vagrant, ...) be sure to use a fresh copy of the extensions's source code for each machine. If that is not possible make sure that you use `make install -B` for the first compilation after a machine switch as the generated object files will not be portable between different machines.

### Windows

You can [download a pre-compiled DLL](https://github.com/martinschroeder/ext-async-win32/raw/master/php_async.dll) working with PHP 7.3+ (VC15 x64 Thread Safe only). Just drop the DLL file in your `ext` directory and add `extension=php_async.dll` in your `php.ini` file.

Building the extension on Windows requires the PHP-SDK for Windows. After the SDK is installed you have to use the starter batch file for you platform and Visual Studio version. In the command prompt navigate to the `php-src` directory and execute these commands and build PHP with the async extension as DLL file:
```shell
buildconf
configure --disable-all --enable-cli --enable-async=shared
nmake
```
You can leave out `=shared` to compile the extension into PHP (faster compilation and no need to load it in `php.ini`) which is nice for testing but does not create a DLL file that can be distributed. You should add `--enable-debug` to allow for debugging when you are working on the source code of the extension.

## INI Settings

| Setting | Description |
| --- | --- |
| `async.dns` | Replaces some internal function (`gethostbyname()` and `gethostbynamel()`) with async implementations. |
| `async.filesystem` | Replaces PHP's `file` stream wrapper with an async implementation. |
| `async.tcp` | (**experimental**) Replaces PHP's `tcp` and `tls` stream wrappers with async implementations. |
| `async.threads` | Sets the maximum number of threads to be used by libuv to run blocking operations without blocking the main thread. The default value is 4 the maximum value is 128. |
| `async.timer` | Replaces PHP's `sleep()` function with an async implementation. |
| `async.udp` | (**experimental**) Replaces PHP's `udp` stream wrapper with an async implementation. |

## Async API

The async extension exposes a public API that can be used to create, run and interact with fiber-based async executions. You can obtain the API stub files for code completion in your IDE by installing `concurrent-php/async-api` via Composer.

### Stream Wrappers

Async access to the filesystem is provided via the `async-file` URL scheme. You can (and should) set INI setting `async.filesystem` to `1` to have (most) file operations by async by default (there are a few exception like `touch()` and `chmod()` which could be fixed in PHP someday).

The async extension provides `async-tcp`, `async-tls` and `async-udp` stream wrappers that can be used to create async PHP stream resources. Use `async-tcp://{server}:{port}/` with `stream_socket_client()` to establish a PHP stream that is backed by `ext-async` and does non-blocking IO (this is not related to `stream_set_blocking()`). You can also use INI settings `async.tcp` and `async.udp` to replace PHP's default stream implementations with their async counterpart which eliminates the need to prefix protocol names with `async-`.

Async stream wrappers have (limited) support for TLS encryption using stream context options:

| Option | Implementation Status |
| --- | --- |
| `crypto_method` | Ignored, ext-async will always use TLS 1.2 and automatically detect server / client mode. |
| `peer_name` | Supported, fallback to host name being used to establish the connection. |
| `verify_peer` | Always enabled! |
| `verify_peer_name` | Always enabled! |
| `allow_self_signed` | Supported, defaults to `false`. |
| `cafile` | Supported |
| `capath` | Supported |
| `local_cert` | Supported |
| `local_pk` | Supported |
| `passphrase` | Supported |
| `CN_match` | Ignored (use `peer_name` instead). |
| `verify_depth` | Supported |
| `ciphers` | Not supported |
| `capture_peer_cert` | Not supported |
| `capture_peer_cert_chain` | Not supported |
| `SNI_enabled` | Supported, defaults to `true`. |
| `SNI_server_name` | Ignored (use `peer_name` instead). |
| `disable_compression` | Ignored, compression is always disabled to protect against CRIME attacks. |
| `peer_fingerprint` | Not supported |
| `alpn_protocols` | Supported |

### Awaitable

This interface cannot be implemented directly by userland classes, implementations are provided by `Deferred::awaitable()` and `Task`. `Awaitable` is exposed as a union type to enable proper type hinting.

```php
namespace Concurrent;

interface Awaitable { }
```

### Deferred

A deferred is a placeholder for an async operation that can be succeeded or failed from userland. It can be used to implement combinator function that operate on multiple `Awaitable` and expose a single `Awaitable` as result. The value returned from `awaitable()` is meant to be consumed by other tasks (or deferreds). The `Deferred` object itself must be kept private to the async operation because it can either succeed or fail the awaitable.

Each `Deferred` may specify a cancellation callback as constructor argument. The callback will be triggered when the current `Context` is cancelled. It receives the `Deferred` object as first argument and the cancellation error as second argument. You must not throw an error from the callback, doing so will trigger a fatal error and terminate the script.

```php
namespace Concurrent;

final class Deferred
{
    public readonly string $status;
    
    public readonly ?string $file;
    
    public readonly ?int $line;

    public function __construct(callable $cancel = null) { }
    
    public function awaitable(): Awaitable { }
    
    public function resolve($val = null): void { }
    
    public function fail(\Throwable $e): void { }
    
    public static function value($val = null): Awaitable { }
    
    public static function error(\Throwable $e): Awaitable { }
    
    public static function combine(array $awaitables, callable $continuation): Awaitable { }
    
    public static function transform(Awaitable $awaitable, callable $transform): Awaitable { }
}
```

### Task

A task is a fiber-based object that executes a PHP function or method on a separate call stack. Tasks are created using `Task::async()` or `TaskScheduler::run()` (and their contextual counterparts). All tasks are associated with a task scheduler as they are created, there is no way to migrate tasks between different schedulers.

Calling `Task::await()` will suspend the current task and await resolution of the given `Awaitable`. If the awaited object is another `Task` it has to be run on the same scheduler, otherwise `await()` will throw an error.

```php
namespace Concurrent;

final class Task implements Awaitable
{
    public readonly string $status;
    
    public readonly ?string $file;
    
    public readonly ?int $line;

    /* Should be replaced with async keyword if merged into PHP core. */
    public static function async(callable $callback, ...$args): Awaitable { }
    
    /* Should be replaced with extended async keyword expression if merged into PHP core. */
    public static function asyncWithContext(Context $context, callable $callback, ...$args): Awaitable { }
    
    /* Should be replaced with await keyword if merged into PHP core. */
    public static function await(Awaitable $awaitable): mixed { }
}
```

### TaskScheduler

The task scheduler manages a queue of ready-to-run tasks and a (shared) event loop that provides support for timers and async IO. It will also keep track of suspended tasks to allow for proper cleanup on shutdown. There is an implicit default scheduler that will be used when `Task::async()` or `Task::asyncWithContext()` is used in PHP code that is not run using one of the public scheduler methods. It is neighter necessary (nor advisable) to create a task scheduler instance yourself. The only exception to that rule are unit tests, each test should use a dedicated task scheduler to ensure proper test isolation.

You can use `run()` or `runWithContext()` to have the given callback be executed as root task within an isolated task scheduler. The run methods will return the value returned from your task callback or throw an error if your task callback throws. The scheduler will allways run all scheduled tasks to completion, even if the callback task you passed is completed before other tasks. The optional inspection callback will be called as soon as the root task (= the callback) is completed and receives an array containing all tasks that have not been completed yet.

```php
namespace Concurrent;

final class TaskScheduler
{
    public static function run(callable $callback, ?callable $inspect = null): mixed { }
    
    public static function runWithContext(Context $context, callable $callback, ?callable $inspect = null): mixed { }
}
```

### Channel

You can use `Channel` objects to exchange messages between different tasks. Every call to `send()` will pause the calling task until the message has been received by another task. A channel can buffer a number of messages specified by `$capacity` within the constructor. If capacity is bigger than 0 calls to `send()` will succedd immediately if there is some space left in the channel's buffer.

Reading from a channel is done using PHP's iterator API. You can use `foreach` to iterate over a channel object. Be sure to call `getIterator()` and return just the created `ChannelIterator` if you want to expose the contents of a channel. This way you prevent thirdparty code from calling `close()` or sending messages into your channel.

```php
namespace Concurrent;

final class Channel implements \IteratorAggregate
{
    public function __construct(int $capacity = 0) { }
    
    public function close(?\Throwable $e = null): void { }
    
    public function isClosed(): bool { }
    
    public function send($message): void { }
}
```

### ChannelGroup

Working with multiple channels at once can be done by using a `ChannelGroup`. The group provides the very useful `select()` call that allows to read from multiple channels concurrently, it will return once a message has been received from any channel that is part of the group. You can use the `$timeout` argument to specify a maximum wait time (in milliseconds). If no message has been received `select()` will return `NULL`. Whenever a message has been received `select()` will return the index of the channel in the wrapped `$channel` array. You have to pass a variable by reference to `select()` if you want to receive the message payload (this is not necessary if you just want to know the origin of the message but you do not care about the actual payload).

The constructor accepts a `$channels` array that must contain eighter `Channel` objects or objects that implement `IteratorAggregate` and return a `ChannelIterator` object when `getIterator()` is called. You can call `count()` to check how many of the wrapped channels are still open and therefore considered to be readable. Keep in mind that you need to call `select()` before checking the count because channels can only be checked for closed state during `select()` (consider using a `do / while` loop and check `count()` as the loop condition). Closed channels are silently removed from the group if they are not closed with an error. If any of the input channels is closed with an error it will be forwarded exactly once by `select()` after that the closed channel will be removed!

You can use `send()` to deliver a message to any of the grouped channels. The first channels that is (or becomes) ready will receive the message. A call to `send()` will return the key of the target channel within the wrapped `$channel` array to let you know which channel received the message. A return value of `NULL` indicates that no channel received a message, this can happen when the timeout is exceeded or no more open channels remain within the group (use `count()` to verify this after(!) a call to `send()`).

```php
namespace Concurrent;

final class ChannelGroup implements \Countable
{
    public function __construct(array $channels, bool $shuffle = false) { }
    
    public function select(?int $timeout = null): ?ChannelSelect { }
    
    public function send($message, ?int $timeout = null): mixed { }
}

final class ChannelSelect
{
    public string $key;
    public string $value;
}
```

### Context

Each async operation is associated with a `Context` object that provides a logical execution context. The context can be used to carry execution-specific variables and (in a later revision) cancellation signals across API boundaries. The context is immutable, a new context must be derived whenever anything needs to be changed for the current execution. You can pass a `Context` to `Task::asyncWithContext()` or `TaskScheduler::runWithContext()` that will become the current context for the new task. It is also possible to enable a context for the duration of a callback execution using the `run()` method. Every call to `Task::await()` will backup the current context and restore it when the task is resumed.

```php
namespace Concurrent;

final class Context
{
    public function isCancelled(): bool { }

    public function with(ContextVar $var, $value): Context { }
    
    public function withIsolatedOutput(): Context { }
    
    public function withTimeout(int $milliseconds): Context { }
    
    public function withCancel(& $cancel): Context { }
    
    public function shield(): Context { }
    
    public function throwIfCancelled(): void { }
    
    public function run(callable $callback, ...$args): mixed { }
    
    public static function current(): Context { }
    
    public static function background(): Context { }
}
```

### ContextVar

You can access contextual data using a `ContextVar` object. Calling `get()` will lookup the variable's value from the context (passed as argument, current context by default). You have to use `Context::with()` to derive a new `Context` that has a value bound to the variable.

```php
namespace Concurrent;

final class ContextVar
{
    public function get(?Context $context = null) { }
}
```

### CancellationHandler

You have to call `Context::withCancel(& $cancel)` to create a new `Context` (returned value) and a `CancellationHandler` placed in `$cancel` (which has to be passed by reference). Invoking the handler will cancel the associated context, the optional error argument will registered as previous error with the thrown `CancellationException`.

```php
namespace Concurrent;

final class CancellationHandler
{
    public function __invoke(?\Throwable $e = null): void { }
}
```

### Thread

You can make use of multiple PHP threads using a `Thread` object. Each thread requires a PHP bootstrap file. You can think of a `Thread` as an embedded additional PHP process that executes in parallel within your process. Threads utilize the same IPC pipe API as forked processes (see `ProcessBuilder`). A worker thread may call `connect()` to obtain an IPC pipe connected to the parent process / thread. The master process / thread can obtain the other end of the pipe by calling `getIpc()`. A call to `join()` will await termination of the `Thread` and return the exit status of the thread (0 on normal termination).

> You need a thread-safe build (ZTS) of PHP in order to make use of threads. This requires PHP to be compiled with `--enable-maintainer-zts`. Usage of threads is also restricted to the `cli` SAPI (PHP running from the command line). An alternative to using threads is to use `ProcessBuilder::fork()` to create additional PHP worker processes and exchange data using IPC pipes.

```php
namespace Concurrent;

use Concurrent\Network\Pipe;

final class Thread
{
    public function __construct(string $file) { }
    
    public static function isAvailable(): bool { }
    
    public static function isWorker(): bool { }
    
    public static function connect(): Pipe { }
    
    public function getIpc(): Pipe { }
    
    public function kill(): void { }
    
    public function join(): int { }
}
```

## Sync API

### Condition

A `Condition` can be used to have a task wait until it is signalled. A task that calls `wait()` will be blocked (async) until some other piece of code calls eighter `signal()` or `broadcast()`. A call to `signal()` will reactivate the first task that is waiting (it has no effect if no task is waiting). Calling `broadcast()` will reactivate all tasks that have been waiting at the moment this method is called. Tasks that enter while `broadcast()` is still working will not be activated. Both `signal()` and `broadcast()` will return the number of waiting tasks that have been rescheduled. A call to `close()` will wake up all waiting tasks with an error. Any attempz to signal or wait on a closed condition will throw an error.

```php
namespace Concurrent\Sync;

final class Condition
{
    public function close(?\Throwable $e = null): void { }
    
    public function wait(): void { }
    
    public function signal(): int { }
    
    public function broadcast(): int { }
}
```

## Watcher API

### Monitor

A `Monitor` can be used to watch the (local) filesystem for changes. You have to provide a valid filesystem path in the constructor. The `Monitor` will raise a `MonitorEvent` for each change of the monitored file or directory. One filesystem change may trigger multiple (possibly identical) events, be prepared to handle this case in your application.

> Change detection is backed by native OS APIs, recursive monitoring is supported on Windows only.

```php
namespace Concurrent;

final class Monitor
{
    public function __construct(string $path, ?bool $recursive = null) { }
    
    public function close(?\Throwable $e = null): void { }
    
    public function awaitEvent(): MonitorEvent { }
}

final class MonitorEvent
{
    public const RENAMED;
    public const CHANGED;
    
    public int $events;
    public string $path;
}
```

### Timer

The `Timer` class is used to schedule timers with the integrated event loop. Timers do not make use of callbacks, instead they will suspend the current task during `awaitTimeout()` and continue when the next timeout is exceeded. The first call to `awaitTimeout()` will start the timer. If additional tasks await an active the timer they will share the same timeout (which could be less than the value passed to the constructor). A `Timer` can be closed by calling `close()` which will fail all pending timeout subscriptions and prevent any further operations.

You can use `timeout()` to create an awaitable that will fail with a `TimeoutException` after the given number of milliseconds have passed. This is mostly useful when deferred combinators are involved and the (preferred) context API (`withTimeout()`) cannot be used.

```php
namespace Concurrent;

final class Timer
{
    public function __construct(int $milliseconds) { }
    
    public function close(?\Throwable $e = null): void { }
    
    public function awaitTimeout(): void { }
    
    public static function timeout(int $milliseconds): Awaitable { }
}
```

### Poll

A `Poll` observes a PHP stream or socket for readability or writability. Only a single poll is allowed for any PHP resource. The poll should be closed when it is no longer needed to free internal resources. The `Poll` will suspend the current task during `awaitReadable()` and `awaitWritable()` and continue once the watched stream becomes readable or is closed by the remote peer. A `Poll` can be used simultaneously (by multiple tasks) to await both read and write events, all tasks will be continued (in the same order as they entered await) once the stream is readable / writable. A `Poll` can be closed by calling `close()` which will fail all pending read & write subscriptions and prevent any further operations.

```php
namespace Concurrent;

final class Poll
{
    public function __construct($resource) { }
    
    public function close(?\Throwable $e = null): void { }
    
    public function awaitReadable(): void { }
    
    public function awaitWritable(): void { }
}
```

### Signal

A `Signal` observes UNIX signals (limited support on Windows). The signal should be closed when it is no longer needed to free internal resources. The current task will be suspended during calls to `awaitSignal()` and continue once the signal has been received. Multiple tasks can await a signal at the same time, all of them will be continued when the signal has been received. You can use `isSupported()` to check if the passed signal can be observed. Windows systems only support `SIGHUP` (console window closed) and `SIGINT` (CTRL + C) handling.

```php
namespace Concurrent;

final class Signal
{
    public const SIGHUP;
    public const SIGINT;
    public const SIGQUIT;
    public const SIGKILL;
    public const SIGTERM;
    public const SIGUSR1;
    public const SIGUSR2;

    public function __construct(int $signum) { }
    
    public function close(?\Throwable $e = null): void { }
    
    public function awaitSignal(): void { }
    
    public static function isSupported(int $signum): bool { }
    
    public static function raise(int $signum): void { }
    
    public static function signal(int $pid, int $signum): void { }
}
```

## Stream API

The stream API provides an object-oriented interface to arbitrary byte streams. The stream API is closely aligned with `fclose()`, `fread()` and `fwrite()` functions.

### ReadableStream

A readable stream provides access to chunks of incoming data. There is no method to check for EOF, a call to `read()` will return `null` when the stream is at EOF. The (optional) `$length` argument can be used to specify the maximum number of bytes to be returned, a stream might return fewer bytes depending on network IO or internal buffers. Every call to `read()` must return at least one bytes, or `null` if no more bytes can be read (EOF). The optional error argument of `close()` allows to pass in an error that will be set as previous error when failing a read operation. Calling `close()` will fail all pending read operations and prevent any further reads from the stream by throwing a `StreamClosedException`.

Only one pending read operation is allowed on a `ReadableStream` at any time. Calls to `read()` must throw a `PendingReadException` if an attempt is made to read from a stream before all previous reads have completed.

```php
namespace Concurrent\Stream;

interface ReadableStream
{
    public function close(?\Throwable $e = null): void;
    
    public function read(?int $length = null): ?string;
}
```

### WritableStream

A writable stream allows to write chunks of data in sequence. Multiple calls to `write()` from different tasks at the same time are allowed, stream implementations must preserve order of write operations. The optional error argument of `close()` allows to pass in an error that will be set as previous error when failing a write operation. Calling `close()` will fail all pending write operations and prevent any further writes from the stream.

```php
namespace Concurrent\Stream;

interface WritableStream
{
    public function close(?\Throwable $e = null): void;
    
    public function write(string $data): void;
}
```

### DuplexStream

The duplex stream implements both `ReadableStream` and `WritableStream`. Streams backed by a socket will usually be compatible with (and implement) this interface. You can call `getReadableStream()` or `getWritableStream()` to aquire a stream that is restricted to one of the combined interfaces. This is especially useful if you want calls to `close()` to result in a half-closed stream.

```php
namespace Concurrent\Stream;

interface DuplexStream extends ReadableStream, WritableStream
{
    public function getReadableStream(): ReadableStream;
    
    public function getWritableStream(): WritableStream;
}
```

### ReadablePipe

Provides non-blocking access to `STDIN` pipe of the PHP process.

```php
namespace Concurrent\Stream;

final class ReadablePipe implements ReadableStream
{
    public static function getStdin(bool $ipc = false): ReadablePipe { }
       
    public function isTerminal(): bool { }
}
```

### WritablePipe

Provides non-blocking access to `STDOUT` and `STDERR` pipes of the PHP process.

```php
namespace Concurrent\Stream;

final class WritablePipe implements WritableStream
{
    public static function getStdout(bool $ipc = false): WritablePipe { }
    
    public static function getStderr(bool $ipc = false): WritablePipe { }
    
    public function isTerminal(): bool { }
}
```

## Network API

The network API provides access to stream and datagram sockets.

### Socket

Defines the basic API that every socket-based component exposes.

```php
namespace Concurrent\Network;

interface Socket
{
    public function close(?\Throwable $e = null): void;
    
    public function flush(): void;

    public function getAddress(): string;

    public function getPort(): ?int;
    
    public function setOption(int $option, $value): bool;
}
```

### SocketStream

Defines the API being provided by reliable connected socket streams.

```php
namespace Concurrent\Network;

use Concurrent\Stream\DuplexStream;

interface SocketStream extends Socket, DuplexStream
{
    public function getRemoteAddress(): string;

    public function getRemotePort(): ?int;
    
    public function isAlive(): bool;
    
    public function getWriteQueueSize(): int;
}
```

### Server

Provides the minimal API being exposed by reliable socket servers.

```php
namespace Concurrent\Network;

interface Server extends Socket
{
    public function accept(): SocketStream;
}
```

### Pipe

A pipe is a `DuplexStream` that is backed by a UNIX domain socket or a named pipe (Windows only). For UNIX domain sockets you have to specify a location within the local filesystem as `$name` for `connect()`. On Windows machines you specify a named pipe (`\\\\{$host}\\pipe\\{$name}`) to connect to (you can also pass the pipe name including the host as `$name` to `connect()`).

```php
namespace Concurrent\Network;

final class Pipe implements SocketStream
{
    public static function connect(string $name, ?string $host = null, ?bool $ipc = null): Pipe { }
    
    public static function import(Pipe $pipe, ?bool $ipc = null): Pipe { }
    
    public static function pair(?bool $ipc = null): array { }
    
    public function export(Pipe $pipe): void { }
}
```

### PipeServer

A pipe server listens for incoming connections on a UNIX domain socket or a named pipe (Windows only). For UNIX domain sockets you have to specify a location within the local filesystem as `$name` for `listen()` (the file must not exist, you may have to delete it first). On Windows machines you have to specify the pipe using `$name` (this must not be a pipe name starting with a double backslash).

```php
namespace Concurrent\Network;

final class PipeServer implements Server
{
    public static function bind(string $name, ?bool $ipc = null): PipeServer { }
    
    public static function listen(string $name, ?bool $ipc = null): PipeServer { }
    
    public static function import(Pipe $pipe, ?bool $ipc = null): PipeServer { }
    
    public function export(Pipe $pipe): void { }
}
```

### TcpSocket

A `TcpSocket` wraps a TCP network conneciton. It implements `DuplexStream` to provide access based on the stream API. Closing a TCP socket will close both read and write sides of the stream. You can use `getWritableStream()` to aquire the writer and call `close()` on it to signal the remote peer that the stream is half-closed, you can still read data from the remote peer until the stream is closed by the remote peer.

A call to `encrypt()` is needed in order to establish TLS connection encryption. You have to pass a `TlsClientEncryption` object to `connect()` if you want to establish an encrypted connection. A call to `encrypt()` will return the negotiated ALPN protocol or NULL when ALPN is not being used.

```php
namespace Concurrent\Network;

final class TcpSocket implements SocketStream
{
    public const NODELAY;
    public const KEEPALIVE;

    public static function connect(string $host, int $port, ?TlsClientEncryption $tls = null): TcpSocket { }
    
    public static function import(Pipe $pipe, ?TlsClientEncryption $tls = null): TcpSocket { }
    
    public static function pair(): array { }
    
    public function export(Pipe $pipe): void { }
    
    public function encrypt(): TlsInfo { }
}
```

### TcpServer

A `TcpServer` listens on a local port for incoming TCP connection attempts until `close()` is called to terminate the server socket. You have to call `accept()` to accept the next pending connection attempt. Each accepted connection is wrapped in a `TcpSocket` that can be used to communicate with the remote peer. Accepted socket connections are not closed when the server is closed, they have to be closed individually by calling `close()` on the `TcpSocket` object.

> You can use `bind()` to create a server that does not listen for incoming connections until `accept()` is called for the first time. This can be useful when you want to create a multi-process server. The master process uses `bind()` to create a server and passes the socket to spawned workers using an IPC pipe. Each worker must call `accept()` at least once to start listening for incoming connections. Be sure to set option `SIMULTANEOUS_ACCEPTS` to `false` in your workers to achieve an even load distribution.

```php
namespace Concurrent\Network;

final class TcpServer implements Server
{
    public const SIMULTANEOUS_ACCEPTS;
    
    public static function bind(string $host, int $port, ?TlsServerEncryption $tls = null, ?bool $reuseport = null): TcpServer { }
    
    public static function listen(string $host, int $port, ?TlsServerEncryption $tls = null, ?bool $reuseport = null): TcpServer { }
    
    public static function import(Pipe $pipe, ?TlsServerEncryption $tls = null): TcpServer { }
    
    public function export(Pipe $pipe): void { }
}
```

### TlsInfo

```php
namespace Concurrent\Network;

final class TlsInfo
{
    public readonly string $protocol;
    
    public readonly string $cipher_name;
    
    public readonly int $cipher_bits;
    
    public readonly ?string $alpn_protocol;
}
```

### TlsClientEncryption

Configures an encrypted (TLS) socket client.

```php
namespace Concurrent\Network;

final class TlsClientEncryption
{
    public function withAllowSelfSigned(bool $allow): TlsClientEncryption { }
    
    public function withVerifyDepth(int $depth): TlsClientEncryption { }
    
    public function withPeerName(string $name): TlsClientEncryption { }
    
    public function withAlpnProtocols(string ...$protocols): TlsClientEncryption { }
    
    public function withCertificateAuthorityPath(string $path): TlsClientEncryption { }
    
    public function withCertificateAuthorityFile(string $file): TlsClientEncryption { }
}
```

### TlsServerEncryption

Configures an encrypted (TLS) socket server.

```php
namespace Concurrent\Network;

final class TlsServerEncryption
{
    public function withDefaultCertificate(string $cert, string $key, ?string $passphrase = null): TlsServerEncryption { }
    
    public function withCertificate(string $host, string $cert, string $key, ?string $passphrase = null): TlsServerEncryption { }
    
    public function withAlpnProtocols(string ...$protocols): TlsServerEncryption { }
    
    public function withCertificateAuthorityPath(string $path): TlsServerEncryption { }
    
    public function withCertificateAuthorityFile(string $file): TlsServerEncryption { }
}
```

### UdpSocket

Provides UDP networking capabilities.

```php
namespace Concurrent\Network;

final class UdpSocket implements Socket
{
    public const TTL;
    public const MULTICAST_LOOP;
    public const MULTICAST_TTL;
    
    public static function bind(string $address, int $port): UdpSocket { }
    
    public static function multicast(string $group, int $port): UdpSocket { }
    
    public function receive(): UdpDatagram { }
    
    public function send(UdpDatagram $datagram): void { }
}
```

### UdpDatagram

Wraps a UDP datagram into a single object to allow for better type-hinting and a single return value.

```php
namespace Concurrent\Network;

final class UdpDatagram
{
    public readonly string $data;
    
    public readonly string $address;
    
    public readonly int $port;

    public function __construct(string $data, string $address, int $port) { }
    
    public function withData(string $data): UdpDatagram { }
    
    public function withPeer(string $address, int $port): UdpDatagram { }
}
```

## Process API

The process API provides tools to spawn processes and communicate with them. This includes setting the work directory, setting environment variables, dealing with input / output, support for signals (limited support on Windows) and awaiting termination (including access to the exit code).

### ProcessBuilder

The `ProcessBuilder` is used to configure the execution environment of a spawned process. The constructor takes the command to be executed, additional arguments can be passed to `execute()` or `start()`. The current working directory of the spawned process can be changed using `withCwd()`, the process will inherit the current working directory by default. You can specify env vars to be passed to the process using `withEnv()`, inheritance of all current env vars can be achieved by passing `true` as second argument `$inherit` (inheritance is disabled by default to protect env vars from leaking into foreign processes).

Each process is spawned with access to three anonymous pipes (`STDIN`, `STDOUT` and `STDERR`). By default all pipes will be ignored (redirected to eighter `/dev/null` or `NUL`) providing no access to process IO. Each pipe can be configured to be inherited from the parent process (`with*Inherited(?int)` methods) or to be used as an async stream (`with*Pipe()` methods) controlled by the parent process.

A call to `execute()` will run the spawned process to completion and return the exit code of the process. This method can only be used if no pipe is configured to be an async stream. If you need to do pipe IO, access the process PID or signal the process you need to call `start()` instead which will return a `Process` object that provides access to a running process.

The process builder provides a convenient way to execute a shell command by using the `shell()` named constructor. It will locate the default system shell (`/bin/sh` or `cmd.exe` depending on your OS) and setup a builder object. In non-interactive mode you have to provide the command as first argument to eighter `execute()` or `start()`. Interactive mode will configure `STDIN`, `STDOUT` and `STDERR` to be async streams. You can send input to the shell using the stream returned by `Process::getStdin()`. It is always possible to configure different IO modes for all pipes.

> Running an interactive shell on Windows will (automatically) append `1>&3` to every command and start the shell with `STDOUT` redirected to `NUL`. This hack allows to avoid `cmd.exe` output and prompt line making there way into command output. You can obtain command output using `Process::getStdout()` as usual (just be careful when you are using output redirection in commands yourself as it might break).

In case you want to create a PHP worker process there is the very handy `fork()` named constructor. It will create a process builder that launches another PHP process using the same PHP executable that is running the parent process. It will also make sure that the child process uses the same INI file and INI settings (passed to the parent process using PHP's `-d` command line option) as the parent process. The child process will have no `STDIN` and inherit both `STDOUT` and `STDERR` from the parent process by default. Using `fork()` has an additional benefit: It can establish an IPC pipe to allow full duplex communication between parent and child process (support for transferring TCP and pipe sockets & servers included). The parent process has access to the IPC pipe by calling `getIpc()` on the `Process` object created by `start()`. In your child process you need to call `Process::connect()` to gain access to the other end of the pipe.

```php
namespace Concurrent\Process;

final class ProcessBuilder
{
    public const STDIN;
    public const STDOUT;
    public const STDERR;
    
    public function __construct(string $command, string ...$args) { }
    
    public static function fork(string $file): ProcessBuilder { }
    
    public static function shell(?bool $interactive = false): ProcessBuilder { }
    
    public function withCwd(string $directory): ProcessBuilder { }
    
    public function withEnv(array $env, ?bool $inherit = null): ProcessBuilder { }
    
    public function withStdinPipe(): ProcessBuilder { }
    
    public function withStdinInherited(?int $fd = null): ProcessBuilder { }
    
    public function withoutStdin(): ProcessBuilder { }
    
    public function withStdoutPipe(): ProcessBuilder { }
    
    public function withStdoutInherited(?int $fd = null): ProcessBuilder { }
    
    public function withoutStdout(): ProcessBuilder { }
    
    public function withStderrPipe(): ProcessBuilder { }
    
    public function withStderrInherited(?int $fd = null): ProcessBuilder { }
    
    public function withoutStderr(): ProcessBuilder { }
    
    public function execute(string ...$args): int { }
    
    public function start(string ...$args): Process { }
}
```

### Process

The `Process` class provides access to a started process. You can use `isRunning()` to check if the process has terminated yet. The process identifier can be accessed using `getPid()`. Iy any pipe was configured to be a pipe it will be accessible via the corresponding getter method. You can send a signal to the process using `signal()`, on Windows systems only `SIGHUP` and `SIGINT` are supported (you should use the class constants defined in `Signal` to avoid magic numbers). Calling `join()` will suspend the current task until the process has terminated and return the exit code of the process.

You can check if the current process has been created using `ProcessBuilder::fork()` by calling `isWorker()`. You need to call `connect()` in order to access the IPC pipe that allows a child process to communicate with it's parent process. The pipe can transfer string data and file descriptors, it is up to you to come up with some sort of IPC protocol on top of this (e.g. using binary framing).

```php
namespace Concurrent\Process;

use Concurrent\Network\Pipe;
use Concurrent\Stream\ReadableStream;
use Concurrent\Stream\WritableStream;

final class Process
{
    public static function isWorker(): bool { }
    
    public static function connect(): Pipe { }
    
    public function isRunning(): bool { }
    
    public function getPid(): int { }
    
    public function getStdin(): WritableStream { }
    
    public function getStdout(): ReadableStream { }
    
    public function getStderr(): ReadableStream { }
    
    public function getIpc(): Pipe { }
    
    public function signal(int $signum): void { }
    
    public function join(): int { }
}
```

## Async / Await Keyword Transformation

The extension provides `Task::async()` and `Task::await()` static methods that are implemented in a way that allows for a very simple transformation to the keywords `async` and `await` which could be introduced into PHP some time in the future. Keywords increase code readability by avoiding static method calls and paranthesis around arguments. The `async` keyword has another big advantage: it avoids the need for creating callbacks in code. You just write your function / method calls as usual and prepend the keyword. This makes it easier to perform static analysis of the source code and IDEs can provide code completion.

```php
$task = async $this->sendRequest($request, $timeout);
$response = await $task;

// The above code would be equivalent to the following:
$task = Task::async(Closure::fromCallable($this, 'sendRequest'), $request, $timeout);
$response = Task::await($task);
```

The example shows a possible syntax for a keyword-based async execution model. The `async` keyword can be prepended to any function or method call to create a `Task` object instead of executing the call directly. The calling scope should be preserved by this operation, hence being consistent with the way method calls work in PHP (no need to create a closure in userland code). The `await` keyword is equivalent to calling `Task::await()` but does not require a function call, it can be implemented as an opcode handler in the Zend VM.

```php
$var = new ContextVar();
$context = Context::current()->with($var, 'foo');

$task = async $context => doSomething($a, $b);
$result = await $task;

// The above code would be equivalent to the following:
$task = Task::asyncWithContext($context, 'doSomething', $a, $b);
$result = Task::await($task);
```

The second example shows how passing a new context to a task would also be possible using the `async` keyword. This would allow for a very simple and readable way to setup tasks in a specific context using a keyword-based syntax.

### Source Transformation Examples

The source transformation needs to consider namespaces and preserve scope. Dealing with namespaces is a problem when it comes to function calls because there is a fallback to the global namespace involved and there is no way to determine the called function in all cases during compilation. Here are some examples of code using `async` / `await` syntax and the transformed source code:

```php
$task = async max(1, 2, 3);
$result = await $task;

$task = \Concurrent\Task::async('max', 1, 2, 3);
$result = \Concurrent\Task::await($task);
``` 
Function calls in global namespace only have to check imported functions, the correct function can be determined at compile time.

```php
namespace Foo;

$task = async bar(1, 2);

$task = \Concurrent\Task::async(\function_exists('Foo\\bar') ? 'Foo\\bar' : 'bar', 1, 2);
```
Unqualified function calls in namespace require runtime evaluation of the function to be called (unless the function is imported via use statement).

```php
namespace Foo;

$context = \Concurrent\Context::inherit(['num' => 321]);
$work = function (int $a): int { return $a + Context::var('num'); };

$result = await async $context => $work(42);

$result = \Concurrent\Task::await(\Concurrent\Task::asyncWithContext(\Closure::fromCallable($work), 42));
```
Calling functions stored in variables requires to keep track of the calling scope because `$work` might contain a method call (or an object with `__invoke()` method) with a visibility other than `public`.
