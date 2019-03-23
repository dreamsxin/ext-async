--TEST--
Deferred combinator validates input array.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
    Task::await(Deferred::combine([], function () {}));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Deferred::combine([123], function () {}));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Deferred::combine([(object)[]], function () {}));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(34) "At least one awaitable is required"
string(36) "All input elements must be awaitable"
string(36) "All input elements must be awaitable"
