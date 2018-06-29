--TEST--
Fiber yielding from iterator.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

class FiberIterator implements Iterator {
    protected $v;
    protected $i = 0;
    
    public function current() { return $this->v; }
    public function key() { return $this->i; }
    public function valid() { return $this->v !== null; }
    public function next() { $this->v = Fiber::yield(); ++$this->i; }
    public function rewind() { $this->v = Fiber::yield(); }
}

$f = new Fiber('iterator_to_array');

$f2 = new Fiber(function (Iterator $it) {
    $result = [];
    
    foreach ($it as $k => $v) {
        $result[$k] = $v;
    }
    
    return $result;
});

$f->start(new FiberIterator());
$f->resume('A');
$f2->start(new FiberIterator());
$f->resume('B');
$f2->resume('X');
var_dump($f->resume());
$f2->resume('Y');
var_dump($f2->resume());

?>
--EXPECTF--
array(2) {
  [0]=>
  string(1) "A"
  [1]=>
  string(1) "B"
}
array(2) {
  [0]=>
  string(1) "X"
  [1]=>
  string(1) "Y"
}
