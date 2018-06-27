--TEST--
Fiber can return object to main thread.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'skip';
?>
--FILE--
<?php

use Concurrent\Fiber;

$f = new Fiber(function () {
    echo "CREATE A\n";
    $a = new class() {
        public function __destruct() {
            echo "DESTROYED\n";
        }
    };
    echo "COMPLETED\n";
    return $a;
});

echo "START\n";
$a = $f->start();
echo "DONE\n=> ";
$a = null;
echo "TERMINATED\n";

?>
--EXPECTF--
START
CREATE A
COMPLETED
DONE
=> DESTROYED
TERMINATED
