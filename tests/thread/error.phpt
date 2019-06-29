--TEST--
Thread will forward error details to master.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$thread = new Thread(__DIR__ . '/assets/error.php');

var_dump($thread->join());

--EXPECTF--
string(8) "GO THROW"

Fatal error: Uncaught RuntimeException: This is an error! in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
int(%d)
