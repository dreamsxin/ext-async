<?php

use Concurrent\Deferred;
use Concurrent\Task;
use Concurrent\TaskScheduler;

require_once __DIR__ . '/vendor/autoload.php';

// Event loop and promise API integration using a scheduler activator and an awaitable adapter.

$loop = \React\EventLoop\Factory::create();

$scheduler = new TaskScheduler();

$scheduler->activator(function (TaskScheduler $scheduler) use ($loop) {
    var_dump('=> RUN');
    
    $loop->futureTick([
        $scheduler,
        'run'
    ]);
});

$scheduler->adapter(function ($val) {
    var_dump('ADAPT <= ' . (is_object($val) ? get_class($val) : gettype($val)));
    
    if (!$val instanceof \React\Promise\PromiseInterface) {
        return $val;
    }
    
    $defer = new Deferred();
    
    $val->done(function ($v) use ($defer) {
        $defer->succeed($v);
    }, function ($e) use ($defer) {
        $defer->fail(($e instanceof \Throwable) ? $e : new \Error((string) $e));
    });
    
    return $defer->awaitable();
});

$defer = new \React\Promise\Deferred();

$work = function (string $title): void {
    var_dump($title);
};

$scheduler->task($work, ['A']);
$scheduler->task($work, ['B']);

$scheduler->task(function () use ($loop) {
    $defer = new Deferred();
    
    $loop->addTimer(.8, function () use ($defer) {
        $defer->succeed('H :)');
    });
    
    var_dump(Task::await($defer->awaitable()));
});

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
