--TEST--
Task will await if the returned value is awaitable.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

var_dump(Task::await(Task::async(function () {
    return Deferred::value(321);
})));

?>
--EXPECT--
int(321)
