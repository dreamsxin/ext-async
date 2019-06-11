--TEST--
Deferred combinator fails with an error when it is disposed.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

try {
    return Task::await(Deferred::combine([
        (new Deferred())->awaitable()
    ], function () { }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async(function () {
        return Task::await(Deferred::combine([
            (new Deferred())->awaitable()
        ], function () { }));
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    return Task::await(Deferred::combine([
        (new Deferred())->awaitable()
    ], function ($d, $l, $k, $e, $v) {
        $d->fail($e);
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    return Task::await(Deferred::combine([
        (new Deferred())->awaitable()
    ], function ($d, $l, $k, $e, $v) {
        throw $e;
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(50) "Awaitable has been disposed before it was resolved"
string(50) "Awaitable has been disposed before it was resolved"
string(50) "Awaitable has been disposed before it was resolved"
string(50) "Awaitable has been disposed before it was resolved"
