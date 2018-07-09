--TEST--
Task can be inlined into another task.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler(null, [
    'num' => 123
]);

$scheduler->task(function () {
    $callback = function () {
        return Context::var('num');
    };

    var_dump($callback());
    
    var_dump(Task::await(Task::async($callback)));

    var_dump($callback());
    
    var_dump(Task::await(Task::asyncWithContext(Context::inherit(['num' => 777]), $callback)));

    var_dump($callback());
    
    try {
        Task::await(Task::async(function () {
            throw new \Error('FAIL!');
        }));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

$scheduler->run();

?>
--EXPECT--
int(123)
int(123)
int(123)
int(777)
int(123)
string(5) "FAIL!"
