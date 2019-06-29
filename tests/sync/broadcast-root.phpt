--TEST--
Condition can boradcast signal.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

use Concurrent\Sync\Condition;

$cond = new Condition();

Task::async(function () use ($cond) {
    (new Timer(10))->awaitTimeout();
    
    var_dump('TRIGGER');
    $cond->broadcast();
    var_dump('DONE');
});

var_dump('WAIT');
$cond->wait();
var_dump('CONTINUE');

--EXPECT--
string(4) "WAIT"
string(7) "TRIGGER"
string(4) "DONE"
string(8) "CONTINUE"
