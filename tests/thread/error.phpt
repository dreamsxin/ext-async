--TEST--
Thread will forward error details to master.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
if (\Concurrent\Thread::isAvailable()) echo 'Test requires ZTS';
?>
--FILE--
<?php

namespace Concurrent;

$thread = new Thread(__DIR__ . '/assets/error.php');

try {
    $thread->join();
} catch (\Throwable $e) {
    echo get_class($e), "\n";
    echo $e->getMessage(), "\n";
    echo $e->getFile(), "\n";
    echo $e->getLine(), "\n";
}

--EXPECTF--
Error
This is an error!
%s
6
