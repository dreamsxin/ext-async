--TEST--
Task will await if the returned value is awaitable.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

try {
	Task::await(Task::async(function () {
	    return Deferred::error(new \Error('FAIL!'));
	}));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(5) "FAIL!"
