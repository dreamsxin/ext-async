--TEST--
Context root can handle errors.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler(null, function (\Throwable $e) {
    var_dump(get_class($e), $e->getMessage());
});

$t = $scheduler->task(function () {});

$t->continueWith(function () {
    throw new \Error('FOO!');
});

$scheduler->run();

$t->continueWith(function () {
    throw new \Exception('BAR!');
});

?>
--EXPECTF--
string(5) "Error"
string(4) "FOO!"
string(9) "Exception"
string(4) "BAR!"
