--TEST--
Deferred does not prevent suspend during continuation.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$defer = new Deferred();

Task::async(function () use ($defer) {
    (new Timer(100))->awaitTimeout();
    
    $defer->resolve(123);
});

var_dump(Task::await(Deferred::combine([$defer->awaitable()], function ($defer) {
    return $defer->resolve(321);
})));

var_dump(Task::await(Deferred::value(777)));


--EXPECT--
int(321)
int(777)
