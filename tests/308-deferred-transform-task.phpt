--TEST--
Deferred transform can tasks as input.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::await($t = Task::async(function () {
    return 222;
}));

var_dump(Task::await(Deferred::transform($t, function ($e, $v) {
    var_dump($e, $v);
    
    return $v * 2;
})));

try {
    Task::await($t = Task::async(function () {
        throw new \Error('FOO!');
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

var_dump(Task::await(Deferred::transform($t, function ($e, $v) {
    var_dump($e->getMessage(), $v);
    
    return 777;
})));

$t = Task::async(function () {
    return 321;
});

var_dump(Task::await(Deferred::transform($t, function ($e, $v) {
    var_dump($e, $v);
    
    return $v * 2;
})));

$t = Task::async(function () {
    throw new \Error('Fail!');
});

try {
    Task::await(Deferred::transform($t, function ($e, $v) {
        var_dump($e->getMessage(), $v);
    
        throw new \Error('Test', 0, $e);
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage(), $e->getPrevious()->getMessage());
}

--EXPECT--
NULL
int(222)
int(444)
string(4) "FOO!"
string(4) "FOO!"
NULL
int(777)
NULL
int(321)
int(642)
string(5) "Fail!"
NULL
string(4) "Test"
string(5) "Fail!"
