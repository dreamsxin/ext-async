--TEST--
Fiber can yield an unreferenced object to main thread.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    echo "YIELD\n";
    Fiber::yield(new class() {
        public function __destruct() {
            echo "DESTROYED\n";
        }
    });
    echo "COMPLETED\n";
});

echo "START\n";
$a = $f->start();
echo "RESUME\n";
$f->resume();
echo "DONE\n=> ";
$a = null;
echo "TERMINATED\n";

?>
--EXPECTF--
START
YIELD
RESUME
COMPLETED
DONE
=> DESTROYED
TERMINATED
