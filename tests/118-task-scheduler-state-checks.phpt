--TEST--
Task scheduler checks state in API methods.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$result = $scheduler->run(function () use ($scheduler) {
    try {
        $scheduler->run(function () { });
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    return 123;
});

var_dump($result);

$result = $scheduler->runWithContext(Context::current(), function () use ($scheduler) {
    try {
        $scheduler->run(function () { });
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
    
    return 321;
});

var_dump($result);

?>
--EXPECT--
string(28) "Scheduler is already running"
int(123)
string(28) "Scheduler is already running"
int(321)
