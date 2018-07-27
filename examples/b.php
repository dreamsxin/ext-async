<?php

// Event loop integration using a customized scheduler.

namespace Concurrent;

register_shutdown_function(function () {
    echo "===> Shutdown function(s) execute here.\n";
});

$work = function (string $title): void {
    var_dump($title);
};

Task::await(Task::async(function () use ($work) {
    $defer = new Deferred();
    
    Task::await(Task::async($work, 'A'));
    Task::await(Task::async($work, 'B'));
    
    Task::async(function () {
        $defer = new Deferred();
        
        (new Timer(function () use ($defer) {
            $defer->resolve('H :)');
        }))->start(1000);
        
        var_dump(Task::await($defer->awaitable()));
    });
    
    Task::async(function () use ($defer) {
        var_dump(Task::await($defer->awaitable()));
    });
    
    (new Timer(function () use ($work, $defer) {
        $defer->resolve('F');
        
        Task::async($work, 'G');
    }))->start(500);
    
    var_dump('ROOT TASK DONE');
}));

Timer::tick(function () use ($work) {
    Task::async($work, 'C');
    
    Timer::tick(function () use ($work) {
        Task::async($work, 'E');
    });
    
    Task::async(function ($v) {
        var_dump(Task::await($v));
    }, Deferred::value('D'));
});

var_dump('=> END OF MAIN SCRIPT');
