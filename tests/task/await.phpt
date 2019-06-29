--TEST--
Task awaiting arbitrary values.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('A');

$defer = new Deferred();
$defer->resolve('B');

var_dump(Task::await($defer->awaitable()));

var_dump(Task::await(Task::async(function (string $x): string {
    return $x;
}, 'C')));

Task::await($t = Task::async(function () {
    return 'D';
}));

var_dump(Task::await($t));

$t = Task::async(function () {
    (new Timer(100))->awaitTimeout();
});

try {
    TaskScheduler::run(function () use ($t) {
        Task::await($t);
    });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
string(60) "Cannot await a task that is running on a different scheduler"
