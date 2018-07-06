--TEST--
Fiber preserves refcount of objects passed to resume().
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$a = new class() {
    public function __destruct() {
        echo "DESTROYED\n";
    }
};

$f = new Fiber(function () {
    echo "YIELD\n";
    $a = Fiber::yield();
    echo "YIELDED\n";
    $a = null;
    echo "COMPLETED\n";
    return $a;
});

echo "START\n";
$f->start();
echo "RESUME\n";
$f->resume($a);
echo "DONE\n=> ";
$a = null;
echo "TERMINATED\n";

?>
--EXPECT--
START
YIELD
RESUME
YIELDED
COMPLETED
DONE
=> DESTROYED
TERMINATED
