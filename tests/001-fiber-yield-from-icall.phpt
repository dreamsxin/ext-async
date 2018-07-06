--TEST--
Fiber yielding from internal function call.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    return array_map(function ($i) {
        return Fiber::yield($i);
    }, ['a', 'b']);
});

$f2 = new Fiber(function () {
    return array_map(function ($i) {
        return Fiber::yield($i);
    }, [1, 2]);
});

var_dump($f->start());
var_dump($f2->start());
var_dump($f->resume(3));
var_dump($f2->resume(4));
var_dump($f->resume(4));
var_dump($f2->resume(6));
?>
--EXPECT--
string(1) "a"
int(1)
string(1) "b"
int(2)
array(2) {
  [0]=>
  int(3)
  [1]=>
  int(4)
}
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(6)
}
