--TEST--
Deferred combine can combine multiple unresolved tasks.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/loop-scheduler.inc';
require_once dirname(__DIR__) . '/lib/functions.php';

TaskScheduler::register(new TimerLoopScheduler($loop = new TimerLoop()));

$d1 = new Deferred();
$d2 = new Deferred();

$t1 = Task::async(function () use ($d1) {
    return Task::await($d1->awaitable());
});

$t2 = Task::async(function () use ($d2) {
    return Task::await($d2->awaitable());
});

$t3 = Task::async(function () {
    return 'C';
});

Task::await($t4 = Task::async(function () {
    return 'D';
}));

$loop->timer(50, function () use ($d2) {
    $d2->resolve('B');
});

$loop->timer(150, function () use ($d1) {
    $d1->resolve('A');
});

var_dump('START');
var_dump(Task::await(all([$t1, $t2, $t3, $t4])));
var_dump('END');

--EXPECT--
string(5) "START"
array(4) {
  [0]=>
  string(1) "A"
  [1]=>
  string(1) "B"
  [2]=>
  string(1) "C"
  [3]=>
  string(1) "D"
}
string(3) "END"
