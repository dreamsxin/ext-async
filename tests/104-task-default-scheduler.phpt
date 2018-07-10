--TEST--
Task will be run on a default scheduler.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$work = function (string $a) {
    return $a;
};

var_dump(Task::await(Task::async($work, ['A'])));
var_dump(Task::await(Task::async($work, ['B'])));

$t = Task::async(function () {
    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        $defer->resolve('C');
    });
    
    return Task::await($defer->awaitable());
});

var_dump(Task::await($t));

$t = Task::async(function () {
    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        $defer->fail(new \Error('D'));
    });
    
    return Task::await($defer->awaitable());
});

try {
    Task::await($t);
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
