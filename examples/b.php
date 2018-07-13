<?php

// Event loop integration using a customized scheduler.

namespace Concurrent;

require_once dirname(__DIR__) . '/vendor/autoload.php';

$loop = \React\EventLoop\Factory::create();

register_shutdown_function(function () {
    echo "===> Shutdown function(s) execute here.\n";
});

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

    protected function stopLoop()
    {
        var_dump('STOP LOOP');
        $this->loop->stop();
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

$work = function (string $title): void {
    var_dump($title);
};

Task::await(Task::async(function () use ($loop, $work) {
    $defer = new \React\Promise\Deferred();
    
    Task::await(all([
        Task::async($work, 'A'),
        Task::async($work, 'B')
    ]));
    
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
    
    var_dump('ROOT TASK DONE');
}));

$loop->futureTick(function () use ($loop, $work) {
    Task::async($work, 'C');
    
    $loop->futureTick(function () use ($work) {
        Task::async($work, 'E');
    });
    
    Task::async(function ($v) {
        var_dump(Task::await($v));
    }, 'D');
});

var_dump('=> END OF MAIN SCRIPT');
