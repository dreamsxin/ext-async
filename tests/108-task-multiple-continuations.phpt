--TEST--
Task with multiple continuation callbacks.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$c = function ($e, $v = null) {
    var_dump($e, $v);
};

$scheduler = new TaskScheduler();

$t = $scheduler->task(function () {
   return 7;
});

$t->continueWith($c);

$t->continueWith(function ($e, ?int $v = null) {
    var_dump($e, 2 * $v);
});

$scheduler->run();

$t->continueWith($c);

?>
--EXPECT--
NULL
int(7)
NULL
int(14)
NULL
int(7)
