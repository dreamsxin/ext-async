--TEST--
Task cannot be constructed in userland code.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

try {
    new Task();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(67) "Call to private Concurrent\Task::__construct() from invalid context"
