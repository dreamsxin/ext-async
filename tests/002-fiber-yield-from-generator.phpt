--TEST--
Fiber yielding from PHP generator.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'skip';
?>
--FILE--
<?php

use Concurrent\Fiber;

$gen = function () {
    for ($i = 0; $i < 3; $i++) {
        yield Fiber::yield($i);
    }
};

$f = new Fiber(function () use ($gen) {
    return iterator_to_array($gen());
});

$f2 = new Fiber('iterator_to_array');

var_dump($f->start());
var_dump($f2->start($gen()));
var_dump($f->resume('A'));
var_dump($f2->resume('X'));
var_dump($f2->resume('Y'));
var_dump($f->resume('B'));
var_dump($f->resume('C'));
var_dump($f2->resume('Z'));
?>
--EXPECTF--
int(0)
int(0)
int(1)
int(1)
int(2)
int(2)
array(3) {
  [0]=>
  string(1) "A"
  [1]=>
  string(1) "B"
  [2]=>
  string(1) "C"
}
array(3) {
  [0]=>
  string(1) "X"
  [1]=>
  string(1) "Y"
  [2]=>
  string(1) "Z"
}
