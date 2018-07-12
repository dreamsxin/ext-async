<?php

use Concurrent\Awaitable;
use Concurrent\Deferred;
use Concurrent\Task;
use Concurrent\TaskScheduler;

require_once __DIR__ . '/vendor/autoload.php';

// Event loop integration using a customized scheduler.

$loop = \React\EventLoop\Factory::create();

TaskScheduler::setDefaultScheduler(new class($loop) extends TaskScheduler {

    protected $loop;

    public function __construct(\React\EventLoop\LoopInterface $loop)
    {
        $this->loop = $loop;
    }

    protected function activate()
    {
        var_dump('=> activate');
        
        $this->loop->futureTick(\Closure::fromCallable([
            $this,
            'dispatch'
        ]));
    }

    protected function runLoop()
    {
        var_dump('START LOOP');
        $this->loop->run();
        var_dump('END LOOP');
    }
});

function adapt(\React\Promise\PromiseInterface $promise): Awaitable
{
    $defer = new Deferred();
    
    $promise->done(function ($v) use ($defer) {
        $defer->resolve($v);
    }, function ($e) use ($defer) {
        $defer->fail(($e instanceof \Throwable) ? $e : new \Error((string) $e));
    });
    
    return $defer->awaitable();
}

Task::await(Task::async(function () use ($loop) {
    $defer = new \React\Promise\Deferred();
    
    $work = function (string $title): void {
        var_dump($title);
    };
    
    Task::async($work, 'A');
    Task::async($work, 'B');
    
    Task::async(function () use ($loop) {
        $defer = new Deferred();
        
        $loop->addTimer(.8, function () use ($defer) {
            $defer->resolve('H :)');
        });
        
        var_dump(Task::await($defer->awaitable()));
    });
    
    Task::async(function () use ($defer) {
        var_dump(Task::await(adapt($defer->promise())));
    });
    
    $loop->addTimer(.05, function () use ($work, $defer) {
        $defer->resolve('F');
        
        Task::async($work, 'G');
    });
    
    $loop->futureTick(function () use ($loop, $work) {
        Task::async($work, 'C');
        
        $loop->futureTick(function () use ($work) {
            Task::async($work, 'E');
        });
        
        Task::async(function ($v) {
            var_dump(Task::await($v));
        }, 'D');
    });
}));

var_dump('DONE');
