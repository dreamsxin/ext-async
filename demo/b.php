<?php

use Concurrent\Awaitable;
use Concurrent\Task;
use Concurrent\TaskScheduler;
use React\EventLoop\Factory;
use React\EventLoop\LoopInterface;
use React\Promise\Deferred;
use React\Promise\PromiseInterface;

require_once __DIR__ . '/vendor/autoload.php';

// Event loop integration using future ticks and a scheduler activator.

$loop = Factory::create();

$scheduler = new TaskScheduler();

$scheduler->activator(function (TaskScheduler $scheduler) use ($loop) {
    var_dump('=> RUN');
    
    $loop->futureTick([
        $scheduler,
        'run'
    ]);
});

class PromiseAdapter implements Awaitable
{
    private $promise;

    public function __construct(PromiseInterface $promise)
    {
        $this->promise = $promise;
    }

    public function continueWith(callable $continuation): void
    {
        $this->promise->done(function ($v) use ($continuation) {
            var_dump('RESOLVE => SUCCESS');
            $continuation(null, $v);
        }, function ($e) use ($continuation) {
            var_dump('RESOLVE => ERROR');
            $continuation(($e instanceof \Throwable) ? $e : new \Error((string) $e));
        });
    }
}

$scheduler->adapter(function ($val) {
    var_dump('ADAPT <= ' . (is_object($val) ? get_class($val) : gettype($val)));
    
    return ($val instanceof PromiseInterface) ? new PromiseAdapter($val) : $val;
});

$a = new class($loop) implements Awaitable {

    private $loop;

    public function __construct(LoopInterface $loop)
    {
        $this->loop = $loop;
    }

    public function continueWith(callable $continuation): void
    {
        $this->loop->addTimer(.8, function () use ($continuation) {
            $continuation(null, 'H :)');
        });
    }
};

$defer = new Deferred();

$work = function (string $title): void {
    var_dump($title);
};

$scheduler->task($work, ['A']);
$scheduler->task($work, ['B']);

$scheduler->task(function (Awaitable $a) {
    var_dump(Task::await($a));
}, [$a]);

$scheduler->task(function () use ($defer) {
    var_dump(Task::await($defer->promise()));
});

$loop->addTimer(.05, function () use ($scheduler, $work, $defer) {
    $defer->resolve('F');
    
    $scheduler->task($work, ['G']);
});

$loop->futureTick(function () use ($loop, $scheduler, $work) {
    $scheduler->task($work, [
        'C'
    ]);
    
    $loop->futureTick(function () use ($scheduler, $work) {
        $scheduler->task($work, [
            'E'
        ]);
    });
    
    $scheduler->task(function ($v) {
        var_dump(Task::await($v));
    }, ['D']);
});

$loop->run();

var_dump('EXIT LOOP');
