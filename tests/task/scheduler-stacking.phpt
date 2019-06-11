--TEST--
Task schedulers can be stacked and unstacked.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {
    Task::await(Task::async('var_dump', 'A'));

    TaskScheduler::run(function () {
        $t = Task::async('var_dump', 'B');
        Task::async('var_dump', 'C');

        Task::await($t);
    });

    Task::await(Task::async('var_dump', 'D'));
    Task::async('var_dump', 'E');
});

Task::await(Task::async('var_dump', 'X'));

--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
string(1) "E"
string(1) "X"
