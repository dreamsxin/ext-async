--TEST--
Context error handlers are cascaded from current context to root context.
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
	$context = Context::inherit()->withErrorHandler(function (\Throwable $e) {
        var_dump(get_class($e), $e->getMessage());
        
        throw new \Exception('BAR!', 0, $e);
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
string(4) "ROOT"
string(9) "Exception"
string(4) "BAR!"
string(4) "DONE"
