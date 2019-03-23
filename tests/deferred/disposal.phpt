--TEST--
Deferred combinator fails with an error when it is disposed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
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
