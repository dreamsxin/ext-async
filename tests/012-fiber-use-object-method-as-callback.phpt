--TEST--
Fiber can use object method as callback.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$o = new class() {
    private $b = 3;
    public function __destruct() {
        echo "DESTROYED\n";
    }
    public function test($a) {
        echo "YIELD\n";
        $b = Fiber::yield($this->b);
        echo "COMPLETED\n";
        return $a + $b;
    }
};

$f = new Fiber([$o, 'test']);

echo "START\n";
$b = $f->start(1);
echo "RESUME\n";
$c = $f->resume($b - 1);
echo "RESULT: $c\n=> ";
$o = null;
echo "TERMINATED\n";

?>
--EXPECTF--
START
YIELD
RESUME
COMPLETED
RESULT: 3
=> DESTROYED
TERMINATED
