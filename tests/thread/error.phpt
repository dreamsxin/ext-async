--TEST--
Thread will forward error details to master.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$thread = new Thread(__DIR__ . '/assets/error.php');

var_dump($thread->join());

--EXPECT--
string(8) "GO THROW"
int(1)
