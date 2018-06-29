--TEST--
Task ending with an error passes error to continuation callback.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$c = function (?\Throwable $e, $v = null) {
    var_dump($e ? $e->getMessage() : NULL, $v);
};

$scheduler = new TaskScheduler();

$t = $scheduler->task(function () {
    throw new \Error('Fail 1');
});

$t->continueWith($c);
$scheduler->run();
$t->continueWith($c);

$scheduler->task(function () {
    Task::await(Task::async(function () {
        throw new \Error('Fail 2');
    }));
})->continueWith($c);

$scheduler->run();

$t = $scheduler->task(function () {
    try {
        Task::await(Task::async(function () {
            throw new \Error('Fail 2');
        }));
    } catch (\Throwable $e) {
        return $e->getMessage();
    }
});

$t->continueWith($c);
$scheduler->run();
$t->continueWith($c);

$e = new \Error('Fail 3');

$scheduler->task(function () use ($e) {
    throw $e;
})->continueWith(function ($ex) use ($e) {
    var_dump($ex === $e);
});

$scheduler->run();

?>
--EXPECTF--
string(6) "Fail 1"
NULL
string(6) "Fail 1"
NULL
string(6) "Fail 2"
NULL
NULL
string(6) "Fail 2"
NULL
string(6) "Fail 2"
bool(true)
