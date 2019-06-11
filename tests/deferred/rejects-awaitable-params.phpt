--TEST--
Deferred does not allow for Awaitable params as resolution values.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('A');

try {
    Deferred::value(Deferred::value());
} catch (\Throwable $e) {
    var_dump('X');
}

var_dump('B');

try {
    (new Deferred())->resolve(Deferred::value());
} catch (\Throwable $e) {
    var_dump('X');
}

var_dump('C');

?>
--EXPECT--
string(1) "A"
string(1) "X"
string(1) "B"
string(1) "X"
string(1) "C"
