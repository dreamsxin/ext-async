--TEST--
Task scheduling using the static task API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->run(function () {
    $t = Task::async(function (string $title) {
        var_dump($title);
    }, 'A');
    
    var_dump($t instanceof Task);
    
    Task::async(function () {
        var_dump('B');
    });
    
    $work = function () {
        var_dump('C');
    };
    
    Task::async($work);
});

?>
--EXPECT--
bool(true)
string(1) "A"
string(1) "B"
string(1) "C"
