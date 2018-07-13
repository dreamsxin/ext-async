--TEST--
Fiber cannot be started multiple times.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    Fiber::yield();
});

$f->start();

var_dump('START');

try {
    $f->start();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

var_dump('DONE');

?>
--EXPECTF--
string(5) "START"
string(48) "Cannot start Fiber that has already been started"
string(4) "DONE"
