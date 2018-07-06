--TEST--
Fiber can return an unreferenced object to main thread.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
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
--EXPECT--
START
CREATE OBJECT
DONE
=> DESTROYED
TERMINATED
