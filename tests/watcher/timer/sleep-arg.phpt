--TEST--
Sleep validates argument.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.timer=1
--FILE--
<?php

namespace Concurrent;

var_dump(sleep(-1));

--EXPECTF--
Warning: sleep(): Number of seconds must be greater than or equal to 0 in %s on line %d
bool(false)
