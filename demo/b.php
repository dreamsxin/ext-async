<?php

use Concurrent\Awaitable;
use Concurrent\Task;
use Concurrent\TaskScheduler;
use React\EventLoop\Factory;
use React\EventLoop\LoopInterface;

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

$a = new class($loop) implements Awaitable {

    protected $loop;

    public function __construct(LoopInterface $loop)
    {
        $this->loop = $loop;
    }

    public function continueWith(callable $continuation): void
    {
        $this->loop->addTimer(.8, function () use ($continuation) {
            $continuation(null, 'G :)');
        });
    }
};

$work = function (string $title): void {
    var_dump($title);
};

$scheduler->task(function () use ($a) {
    var_dump(Task::await($a));
});

$scheduler->task($work, ['A']);
$scheduler->task($work, ['B']);

$loop->addTimer(.05, function () use ($scheduler, $work) {
    $scheduler->task($work, ['F']);
});

$loop->futureTick(function () use ($loop, $scheduler, $work) {
    $scheduler->task($work, ['C']);
    
    $loop->futureTick(function () use ($scheduler, $work) {
        $scheduler->task($work, ['E']);
    });
    
    $scheduler->task($work, ['D']);
});

$loop->run();

var_dump('EXIT LOOP');
