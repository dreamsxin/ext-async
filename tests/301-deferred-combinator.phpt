--TEST--
Deferred awaitable combinator API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$a = $scheduler->run(function () {
    var_dump('A');

    return Task::await(Deferred::combine([
        Deferred::value('D'),
        Deferred::value(777)
    ], function (Deferred $defer, bool $last, int $index, ?\Throwable $e, $v) {
        var_dump('B');
        var_dump($last);
        var_dump($e);
        $defer->resolve($v);
        var_dump('C');
    }));
});

var_dump($a);

?>
--EXPECT--
string(1) "A"
string(1) "B"
bool(false)
NULL
string(1) "C"
string(1) "B"
bool(true)
NULL
string(1) "C"
string(1) "D"
