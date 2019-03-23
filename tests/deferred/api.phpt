--TEST--
Deferred basic API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$a = TaskScheduler::run(function () {
	return Task::await(Deferred::value(321));
});

var_dump($a);

try {
    var_dump(TaskScheduler::run(function () {
        $e = Deferred::error(new \Error('Fail!'));
    
        var_dump('X');
    
        return Task::await($e);
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

$a = TaskScheduler::run(function () {
    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        var_dump('B');
        
        $defer->resolve(777);
        
        var_dump($defer->__debugInfo()['status']);
        var_dump($defer->awaitable()->__debugInfo()['status']);
    });
    
    var_dump('A');
    
    var_dump($defer->__debugInfo()['status']);
    var_dump($defer->awaitable()->__debugInfo()['status']);
        
    return Task::await($defer->awaitable());
});

var_dump($a);

var_dump(TaskScheduler::run(function () {
    $defer = new Deferred();
    $defer->resolve();
    
    return Task::await($defer->awaitable());
}));

?>
--EXPECT--
int(321)
string(1) "X"
string(5) "Fail!"
string(1) "A"
string(7) "PENDING"
string(7) "PENDING"
string(1) "B"
string(8) "RESOLVED"
string(8) "RESOLVED"
int(777)
NULL
