--TEST--
Timer will suspend and resume root execution.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$timer = new Timer(50);

var_dump('START');

for ($i = 0; $i < 3; $i++) {
    $timer->awaitTimeout();
    
    var_dump($i);
}

var_dump('DONE');

--EXPECT--
string(5) "START"
int(0)
int(1)
int(2)
string(4) "DONE"
