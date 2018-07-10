--TEST--
Task with multiple continuations.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->run(function () {
    $t = Task::async(function () {
        return 123;
    });

    $a = Task::async(function () use ($t) {
        return Task::await($t);
    });

    $b = Task::async(function () use ($t) {
        return Task::await($t);
    });
    
    var_dump(Task::await($a));
    var_dump(Task::await($b));
    var_dump(Task::await($t));
});

$scheduler->run(function () {
    $t = null;

    $a = Task::async(function () use (& $t) {
        return Task::await($t);
    });

    $b = Task::async(function () use (& $t) {
        return Task::await($t);
    });
    
    $t = Task::async(function () {
        return 777;
    });
    
    var_dump(Task::await($a));
    var_dump(Task::await($b));
    var_dump(Task::await($t));
});

?>
--EXPECT--
int(123)
int(123)
int(123)
int(777)
int(777)
int(777)
