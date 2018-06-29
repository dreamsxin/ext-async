--TEST--
Task adapter is called to transform non-awaitable objects.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->adapter(function ($v) {
    var_dump('ADAPT');
    
    if ($v instanceof \Error) {
        return $v->getMessage();
    }
    
    if ($v instanceof \Closure) {
        return new \stdClass();
    }
    
    if ($v instanceof \Exception) {
        return new class() implements Awaitable {
            public function continueWith(callable $c) {
                $c(null, 'Done :)');
            }
        };
    }
    
    return $v;
});

$scheduler->task(function () {
    var_dump(Task::await(321));
});

$scheduler->task(function () {
    var_dump(Task::await(new \stdClass()) instanceof \stdClass);
});

$scheduler->task(function () {
    var_dump(Task::await(new \Error('Fail')));
});

$scheduler->task(function () {
    var_dump(Task::await(function () {}) instanceof \stdClass);
});

$scheduler->task(function () {
    var_dump(Task::await(new \Exception('Fail')));
});

$scheduler->run();

?>
--EXPECTF--
int(321)
string(5) "ADAPT"
bool(true)
string(5) "ADAPT"
string(4) "Fail"
string(5) "ADAPT"
bool(true)
string(5) "ADAPT"
string(7) "Done :)"
