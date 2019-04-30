--TEST--
Sleep validates argument.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--INI--
async.timer=1
--FILE--
<?php

namespace Concurrent;

var_dump(sleep(-1));

--EXPECTF--
Warning: sleep(): Number of seconds must be greater than or equal to 0 in %s on line %d
bool(false)
