--TEST--
Sleep function is replaced with an async version.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.timer=1
--FILE--
<?php

namespace Concurrent;

Task::async(function () {
    var_dump(sleep(1));
    var_dump('A');
    sleep(2);
    var_dump('C');
});

Task::async(function () {
    sleep(2);
    var_dump('B');
});

--EXPECT--
NULL
string(1) "A"
string(1) "B"
string(1) "C"
