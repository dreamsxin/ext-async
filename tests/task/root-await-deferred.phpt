--TEST--
Task await on root level will not need a scheduler for resolved deferreds.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump(Task::await(Deferred::value(321)));

try {
    Task::await(Deferred::error(new \Error('Fail!')));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

$defer = new Deferred();

Task::async(function () use ($defer) {
    $defer->resolve(777);
});

var_dump(Task::await($defer->awaitable()));

?>
--EXPECT--
int(321)
string(5) "Fail!"
int(777)
