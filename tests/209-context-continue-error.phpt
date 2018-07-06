--TEST--
Context can call success continuations.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->task(function () {
	$context = Context::current()->withErrorHandler(function (\Throwable $e) {
	    var_dump(get_class($e), $e->getMessage());
	});
	
	var_dump($context->continueError(function (\Throwable $e) {
	    var_dump($e->getMessage());
	    
	    return 'A';
	}, new \Exception('FOO')));
	
	var_dump($context->continueError(function (\Throwable $e) {	    
	    throw $e;
	}, new \Error('Fail!')));
});

$scheduler->run();

?>
--EXPECT--
string(3) "FOO"
NULL
string(5) "Error"
string(5) "Fail!"
NULL
