--TEST--
Task execution does not clear global exception.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::async(function () {
    var_dump(123);
});

set_exception_handler(function ($e) {
    var_dump($e->getMessage());
});

throw new \Exception('FOO!');

--EXPECT--
int(123)
string(4) "FOO!"
