--TEST--
Task will await if the returned value is awaitable.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$t = Task::async(function () {
    return Task::await(Deferred::error(new \Error('FAIL!')));
});

(new Timer(10))->awaitTimeout();

try {
	Task::await($t);	
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

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
string(5) "FAIL!"
