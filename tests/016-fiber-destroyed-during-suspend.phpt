--TEST--
Fiber in suspended state will be cleaned up by throwing into it.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    var_dump('START');
    
    try {
        Fiber::yield();
    } catch(\Throwable $e) {
        var_dump($e->getMessage());
        
        try {
            Fiber::yield();
        } catch (\Throwable $e) {
            var_dump($e->getMessage());
            
            throw $e;
        }
    } finally {
        var_dump('DONE');
    }
});

$f->start();

var_dump('DISPOSE');

$f = null;

var_dump('SCRIPT END');

?>
--EXPECTF--
string(5) "START"
string(7) "DISPOSE"
string(24) "Fiber has been destroyed"
string(45) "Cannot yield from a fiber that is not running"
string(4) "DONE"
string(10) "SCRIPT END"
