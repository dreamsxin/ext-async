--TEST--
Fiber class can display info about used backend.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

use Concurrent\Fiber;

var_dump(Fiber::backend());

?>
--EXPECTF--
string(%d) "%s (%s)"
