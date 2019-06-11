--TEST--
Timer can create an awaitable that will timeout.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$t = Timer::timeout(5000);

try {
    Task::await(Timer::timeout(20));
} catch (TimeoutException $e) {
    var_dump($e->getMessage());
}

$t = null;

--EXPECT--
string(19) "Operation timed out"
