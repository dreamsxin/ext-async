--TEST--
Context root default error handler will trigger fatal error.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->task(function () {})->continueWith(function () {
    throw new \Error('FOO!');
});

var_dump('START');

$scheduler->run();

var_dump('DONE');

?>
--EXPECTF--
string(5) "START"

Warning: Uncaught Error: FOO! in %s203-context-root-fatal-error.php:%d
Stack trace:
#0 [internal function]: Concurrent\{closure}(NULL, NULL)
#1 %s203-context-root-fatal-error.php(%d): Concurrent\TaskScheduler->run()
#2 {main}
  thrown in %s203-context-root-fatal-error.php on line %d

Fatal error: Uncaught awaitable continuation error in %s203-context-root-fatal-error.php on line %d
