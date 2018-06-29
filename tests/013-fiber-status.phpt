--TEST--
Fiber will expose status.
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

var_dump($f->status() == Fiber::STATUS_INIT);
$f->start();
var_dump($f->status() == Fiber::STATUS_SUSPENDED);
$f->resume();
var_dump($f->status() == Fiber::STATUS_FINISHED);

try {
    $f->resume();
} catch (Throwable $e) {
    echo $e->getMessage(), PHP_EOL;
}

var_dump($f->status() == Fiber::STATUS_FINISHED);

$f = new Fiber(function () {
    throw new Exception('FOO!');
});
try {
    $f->start();
} catch (Throwable $e) {
    echo $e->getMessage(), PHP_EOL;
}
var_dump($f->status() == Fiber::STATUS_DEAD);

?>
--EXPECTF--
bool(true)
bool(true)
bool(true)
Non-suspended Fiber cannot be resumed
bool(true)
FOO!
bool(true)
