--TEST--
Task default scheduler can await awaitables created by a deferred.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump(Task::await(Deferred::value(321)));

try {
    Task::await(Deferred::error(new \Error('Fail 1')));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

$defer = new Deferred();

Task::async(function () use ($defer) {
    $defer->resolve(777);
});

var_dump(Task::await($defer->awaitable()));

$defer = new Deferred();

Task::async(function () use ($defer) {
    $defer->fail(new \Error('Fail 2'));
});

try {
    Task::await($defer->awaitable());
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
int(321)
string(6) "Fail 1"
int(777)
string(6) "Fail 2"
