--TEST--
Timer can create an awaitable that will timeout.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$t = Timer::timeout(5000);

try {
    Task::await(Timer::timeout(20));
} catch (TimeoutException $e) {
    var_dump($e->getMessage());
}

$t = null;

--EXPECT--
string(19) "Operation timed out"
