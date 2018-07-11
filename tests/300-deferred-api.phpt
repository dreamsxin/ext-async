--TEST--
Deferred basic API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$a = $scheduler->run(function () {
	return Task::await(Deferred::value(321));
});

var_dump($a);

try {
    var_dump($scheduler->run(function () {
        $e = Deferred::error(new \Error('Fail!'));
    
        var_dump('X');
    
        return Task::await($e);
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

$a = $scheduler->run(function () {
    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        var_dump('B');
        
        $defer->resolve(777);
    });
    
    var_dump('A');
    
    return Task::await($defer->awaitable());
});

var_dump($a);

?>
--EXPECT--
int(321)
string(1) "X"
string(5) "Fail!"
string(1) "A"
string(1) "B"
int(777)
