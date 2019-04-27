--TEST--
Condition can boradcast signal.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

use Concurrent\Sync\Condition;

$cond = new Condition();

Task::async(function () use ($cond) {
    $cond->wait();
    var_dump('A');
    $cond->wait();
    var_dump('C');
});

Task::async(function () use ($cond) {
    $cond->wait();
    var_dump('B');
});

(new Timer(50))->awaitTimeout();

var_dump('BROADCAST');
var_dump($cond->broadcast());

(new Timer(10))->awaitTimeout();

var_dump('SIGNAL');
var_dump($cond->signal());
var_dump($cond->signal());

--EXPECT--
string(9) "BROADCAST"
int(2)
string(1) "A"
string(1) "B"
string(6) "SIGNAL"
string(1) "C"
int(1)
int(0)
