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
	
	var_dump($context->continueSuccess(function ($e, $v) {
	    var_dump($e, $v);
	    
	    return 'A';
	}));
	
	var_dump($context->continueSuccess(function ($e, $v) {
	    var_dump($e, $v);
	    
	    throw new \Error('Fail!');
	}, 777));
});

$scheduler->run();

?>
--EXPECT--
NULL
NULL
NULL
NULL
int(777)
string(5) "Error"
string(5) "Fail!"
NULL
