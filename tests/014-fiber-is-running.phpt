--TEST--
Fiber class indicates if currently executed code is running in a fiber.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    var_dump(Fiber::isRunning());
    Fiber::yield();
    var_dump(Fiber::isRunning());
});

var_dump(Fiber::isRunning());
$f->start();
var_dump(Fiber::isRunning());
$f->resume();
var_dump(Fiber::isRunning());

?>
--EXPECTF--
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
