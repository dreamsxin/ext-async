--TEST--
Context can provide an additional error handler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->task(function () {
	$context = Context::inherit()->withErrorHandler(function (\Throwable $e) {
        var_dump(get_class($e), $e->getMessage());
    });
    
    $task = Task::asyncWithContext($context, function () {});
    
    $task->continueWith(function () {
        throw new \Error("FOO!");
    });
});

var_dump('START');

$scheduler->run();

var_dump('DONE');

?>
--EXPECTF--
string(5) "START"
string(5) "Error"
string(4) "FOO!"
string(4) "DONE"
