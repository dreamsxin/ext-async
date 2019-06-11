--TEST--
Thread will check if bootstrap file exists.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

try {
    new Thread(__DIR__ . '/dummy.php');
} catch (\Throwable $e) {
    echo $e->getMessage();
}

--EXPECTF--
Failed to locate thread bootstrap file: %s
