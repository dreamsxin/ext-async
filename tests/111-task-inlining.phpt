--TEST--
Task can be inlined into another task.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler([
    'num' => 123
]);

$scheduler->task(function () {
    $callback = function () {
        return Context::var('num');
    };
    
    $cont = function ($e, $v) {
        var_dump($e, $v);
    };

    var_dump($callback());
    
    $t = Task::async($callback);
    $t->continueWith($cont);
    
    var_dump(Task::await($t));
    
    $t->continueWith($cont);

    var_dump($callback());

    $t = $t = Task::asyncWithContext(Context::inherit(['num' => 777]), $callback);

    $t->continueWith($cont);
    var_dump(Task::await($t));    
    $t->continueWith($cont);

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
NULL
int(123)
int(123)
NULL
int(123)
int(123)
NULL
int(777)
int(777)
NULL
int(777)
int(123)
string(5) "FAIL!"
