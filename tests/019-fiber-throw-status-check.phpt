--TEST--
Fiber only accepts throw while in suspended state.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    var_dump('DONE');
});

$f->start();

var_dump('BEFORE');

try {
    $f->throw($e = new \Error('EXIT'));
} catch (\Throwable $ex) {
    var_dump($ex === $e);
}

var_dump('DONE');

?>
--EXPECTF--
string(4) "DONE"
string(6) "BEFORE"
bool(true)
string(4) "DONE"
