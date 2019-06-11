--TEST--
Thread will execute a script file in an isolated thread.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump(Thread::isAvailable());
var_dump('START');

$thread = new Thread(__DIR__ . '/assets/fork.php');
var_dump($thread->join());

var_dump('DONE');

--EXPECT--
bool(true)
string(5) "START"
string(5) "READY"
string(9) "TERMINATE"
int(0)
string(4) "DONE"
