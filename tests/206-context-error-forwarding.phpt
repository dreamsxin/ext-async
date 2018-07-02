--TEST--
Context can forward error to context error handler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler(null, function (\Throwable $e) {
    var_dump('ROOT', get_class($e), $e->getMessage());
});

$scheduler->task(function () {
	Context::handleError(new \Error('FOO!'));
});

var_dump('START');

$scheduler->run();

var_dump('DONE');

?>
--EXPECTF--
string(5) "START"
string(4) "ROOT"
string(5) "Error"
string(4) "FOO!"
string(4) "DONE"
