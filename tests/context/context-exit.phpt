--TEST--
Context does support calling exit during run.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('A');

Context::current()->run(function () {
    var_dump('B');
    exit();
    var_dump('C');
});

var_dump('D');

?>
--EXPECT--
string(1) "A"
string(1) "B"
