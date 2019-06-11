--TEST--
Task being suspended and resumed.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::async('var_dump', 'A');

$defer = new Deferred();

Task::async(function () use ($defer) {
    var_dump(Task::await($defer->awaitable()));
});

Task::async('var_dump', 'B');

Task::async(function () use ($defer) {
    $defer->resolve('C');

    var_dump('D');
});

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
