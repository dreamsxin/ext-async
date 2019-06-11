--TEST--
Deferred combine can combine multiple unresolved tasks.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

function all(array $awaitables): Awaitable
{
    $result = \array_fill_keys(\array_keys($awaitables), null);
    
    $all = function (Deferred $defer, $last, $k, $e, $v) use (& $result) {
        if ($e) {
            $defer->fail($e);
        } else {
            $result[$k] = $v;
            
            if ($last) {
                $defer->resolve($result);
            }
        }
    };
    
    return Deferred::combine($awaitables, $all);
}

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

Task::async(function () use ($d1, $d2) {
    $timer = new Timer(60);
    $timer->awaitTimeout();
    
    var_dump('T1');
    $d2->resolve('B');
    
    $timer->awaitTimeout();
    
    var_dump('T2');
    $d1->resolve('A');
});

var_dump('START');
var_dump(Task::await(all([$t1, $t2, $t3, $t4])));
var_dump('END');

--EXPECT--
string(5) "START"
string(2) "T1"
string(2) "T2"
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
