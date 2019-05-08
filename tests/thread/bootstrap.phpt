--TEST--
Thread will check if bootstrap file exists.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
if (\Concurrent\Thread::isAvailable()) echo 'Test requires ZTS';
?>
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
