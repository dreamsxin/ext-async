--TEST--
Fiber preserves refcount of objects passed to start().
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

$f = new Fiber(function ($a) {
    echo "UNSET\n";
    $a = null;
    echo "COMPLETED\n";
    return $a;
});

echo "START\n";
$f->start($a);
echo "DONE\n=> ";
$a = null;
echo "TERMINATED\n";

?>
--EXPECTF--
START
UNSET
COMPLETED
DONE
=> DESTROYED
TERMINATED
