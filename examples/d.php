<?php

namespace Concurrent;

error_reporting(-1);
ini_set('display_errors', '1');

$scheduler = new TaskScheduler();

$scheduler->task(function () {
    $defer = new Deferred();

    $t = Task::async(function () use ($defer) {
        var_dump('INNER DONE!');
        $defer->resolve(777);
        return 777;
    });
    var_dump('ICKE!');
    var_dump(Task::await($defer->awaitable()));
    var_dump(Task::await($t));
    var_dump('OUTER DONE!');
});

$scheduler->run();
