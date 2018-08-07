<?php

namespace Concurrent;

$handler = new CancellationHandler(Context::current());

Task::asyncWithContext($handler->context(), function () {
    $defer = new Deferred(function (Deferred $defer, ?\Throwable $e = null) {
        var_dump('CANCEL ME!');
        
        $defer->resolve(777);
    });
    
    var_dump(Task::await($defer->awaitable()));
});

$timer = new Timer(100);
$timer->awaitTimeout();

var_dump('KILLIT!');
$handler->cancel();
