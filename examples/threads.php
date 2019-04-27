<?php

namespace Concurrent;

var_dump(ThreadPool::isAvailable());

$v = call_user_func(function () {
    $pool = new ThreadPool(2, __DIR__ . '/threads-bootstrap.php');

    $job1 = $pool->submit(function () {
        sleep(1);
        return 'Hello';
    });

    var_dump('GO');
    (new Timer(100))->awaitTimeout();
    var_dump('NAU');

    $job2 = $pool->submit(function (string $x) {
        return subject($x);
    }, 'world');

    $pool->close();

    var_dump(Task::await($job1));
    return Task::await($job2);
});

var_dump($v);
(new Timer(1000))->awaitTimeout();

var_dump('NO GO...', $v);

$pool = null;

TaskScheduler::run(function () use (& $pool) {
    $pool = new ThreadPool();

    $job = $pool->submit(function (int $a, int $b): int {
        return $a + $b;
    }, 1, 2);

    var_dump(Task::await($job));
});

try {
    $pool->submit(function () {});
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

var_dump('DONE ALL');
