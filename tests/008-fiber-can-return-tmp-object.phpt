--TEST--
Fiber can return an unreferenced object to main thread.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'skip';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    echo "CREATE OBJECT\n";
    return new class() {
        public function __destruct() {
            echo "DESTROYED\n";
        }
    };
});

echo "START\n";
$a = $f->start();
echo "DONE\n=> ";
$a = null;
echo "TERMINATED\n";

?>
--EXPECTF--
START
CREATE OBJECT
DONE
=> DESTROYED
TERMINATED
