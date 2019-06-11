--TEST--
Thread can be killed using interrupt.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('START');

$thread = new Thread(__DIR__ . '/assets/kill.php');

(new Timer(200))->awaitTimeout();

$thread->kill();
$thread->join();

var_dump('DONE');

--EXPECT--
string(5) "START"
string(3) "RUN"
string(4) "DONE"
