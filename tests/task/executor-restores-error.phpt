--TEST--
Task scheduler is disposed if a root-level error occurs.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::async(function () {
    var_dump(123);
});

throw new \Exception('FOO!');

--EXPECTF--
Fatal error: Uncaught Exception: FOO! in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
