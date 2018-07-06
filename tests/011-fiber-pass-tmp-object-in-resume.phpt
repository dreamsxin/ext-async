--TEST--
Fiber can pass an unreferenced object using resume().
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    echo "YIELD\n";
    $a = Fiber::yield();
    echo "YIELD\n";
    Fiber::yield();
    echo "COMPLETED\n=> ";
});

echo "START\n";
$f->start();
echo "RESUME\n";
$f->resume(new class() {
    public function __destruct() {
        echo "DESTROYED\n";
    }
});
echo "RESUME\n";
$f->resume();
echo "TERMINATED\n";

?>
--EXPECT--
START
YIELD
RESUME
YIELD
RESUME
COMPLETED
=> DESTROYED
TERMINATED
