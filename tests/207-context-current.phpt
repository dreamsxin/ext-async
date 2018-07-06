--TEST--
Context provides access to current context.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->task(function () {
	$context = Context::current();
	var_dump($context instanceof Context);
	
	Task::await(Task::async(function () use ($context) {
	    var_dump($context === Context::current());
	}));
	
	$ctx = Context::inherit();
	
	var_dump($context === $ctx);
	
	Task::await(Task::asyncWithContext($ctx, function () use ($ctx) {
	    var_dump($ctx === Context::current());
	}));
	
	var_dump($context === Context::current());
});

$scheduler->run();

?>
--EXPECTF--
bool(true)
bool(true)
bool(false)
bool(true)
bool(true)
