--TEST--
Deferred awaitable combinator API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$a = TaskScheduler::run(function () {
    var_dump('A');

    return Task::await(Deferred::combine([
        0 => Deferred::value('D'),
        'X' => Deferred::value(777)
    ], function (Deferred $defer, bool $last, $index, ?\Throwable $e, $v) {
        var_dump('B');
        var_dump($last);
        var_dump($index);
        var_dump($e);
        
        if ($last) {
            $defer->resolve($v);
        }
        
        var_dump('C');
    }));
});

var_dump($a);

?>
--EXPECT--
string(1) "A"
string(1) "B"
bool(false)
int(0)
NULL
string(1) "C"
string(1) "B"
bool(true)
string(1) "X"
NULL
string(1) "C"
int(777)
