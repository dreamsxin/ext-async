--TEST--
Task with multiple continuations.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

var_dump('START');

TaskScheduler::run(function () {
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

var_dump('MIDDLE');

TaskScheduler::run(function () {
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

var_dump('END');

?>
--EXPECT--
string(5) "START"
int(123)
int(123)
int(123)
string(6) "MIDDLE"
int(777)
int(777)
int(777)
string(3) "END"
