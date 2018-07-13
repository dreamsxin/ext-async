--TEST--
Fiber callback will not be run if disposed before start.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(new class() {
    public function __destruct() {
        var_dump('DTOR');
    }
    
    public function __invoke() {
        var_dump('START');
    }
});

var_dump('DISPOSE');

$f = null;

var_dump('SCRIPT END');

?>
--EXPECTF--
string(7) "DISPOSE"
string(4) "DTOR"
string(10) "SCRIPT END"
