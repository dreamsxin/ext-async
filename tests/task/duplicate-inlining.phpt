--TEST--
Task prevents multiple inlining of the same task instance.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::async(function () use (&$awaitable) {
    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        (new Timer(50))->awaitTimeout();
    
        $defer->resolve(123);
    });
    
    $awaitable = Task::async(function () use ($defer) {
        return Task::await($defer->awaitable());
    });
});

Task::async(function () use (&$awaitable) {
    var_dump(Task::await($awaitable));
});

Task::async(function () use (&$awaitable) {
    var_dump(Task::await($awaitable));
});

--EXPECT--
int(123)
int(123)
