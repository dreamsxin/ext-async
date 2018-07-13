--TEST--
Fiber can receive an error thrown into it while suspended.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    try {
        Fiber::yield();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    try {
        Fiber::yield(123);
    } finally {
        var_dump('END');
    }
});

$f->start();

var_dump('BEFORE');

var_dump($f->throw(new \Error('Fail!')));

try {
    $f->throw(new \Error('EXIT'));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECTF--
string(6) "BEFORE"
string(5) "Fail!"
int(123)
string(3) "END"
string(4) "EXIT"
