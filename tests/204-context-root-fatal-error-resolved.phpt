--TEST--
Context root error handler will trigger fatal error even if task is already completed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$t = $scheduler->task(function () {});

var_dump('START');

$scheduler->run();

var_dump('READY');

$t->continueWith(function () {
    throw new \Error('FOO!');
});

var_dump('DONE');

?>
--EXPECTF--
string(5) "START"
string(5) "READY"

Warning: Uncaught Error: FOO! in %s204-context-root-fatal-error-resolved.php:%d
Stack trace:
#0 [internal function]: Concurrent\{closure}(NULL, NULL)
#1 %s204-context-root-fatal-error-resolved.php(%d): Concurrent\Task->continueWith(Object(Closure))
#2 {main}
  thrown in %s204-context-root-fatal-error-resolved.php on line %d

Fatal error: Uncaught awaitable continuation error in %s204-context-root-fatal-error-resolved.php on line %d
