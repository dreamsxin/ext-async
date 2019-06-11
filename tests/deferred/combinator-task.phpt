--TEST--
Deferred combinator can await tasks.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::await($t = Task::async(function () {
    return 1;
}));

try {
    Task::await($f = Task::async(function () {
        throw new \Error('Fail!');
    }));
} catch (\Throwable $e) {
}

Task::await(Deferred::combine([
    'A' => $t,
    'B' => $f,
    'C' => Task::async(function () {
        return 3;
    })
], function (Deferred $defer, $last, $k, $e, $v) {
    var_dump($k, $v);

    if ($e) {
        var_dump($e->getMessage());
    }

    if ($last) {
        $defer->resolve();
    }
}));

--EXPECT--
string(1) "A"
int(1)
string(1) "B"
NULL
string(5) "Fail!"
string(1) "C"
int(3)
