--TEST--
Deferred does not allow for Awaitable params as resolution values.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
    Deferred::value(Deferred::value());
} catch (\Throwable $e) {
    var_dump('X');
}

try {
    (new Deferred())->resolve(Deferred::value());
} catch (\Throwable $e) {
    var_dump('X');
}

?>
--EXPECT--
string(1) "X"
string(1) "X"
