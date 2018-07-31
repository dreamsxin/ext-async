--TEST--
Task can only enter wait mode if the dispatcher loop is running.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::run(function () {
    (new Timer(function () {
        var_dump('TIMER');

        try {
            var_dump(Task::await(Deferred::value(321)));
        } catch (\Throwable $e) {
            var_dump($e->getMessage());
        }
    }))->start(10);

    var_dump('TASK');
});

var_dump('DONE');

TaskScheduler::run(function () {
    TaskScheduler::run(function () {
        (new Timer(function () {
            var_dump('TIMER');

            try {
                var_dump(Task::await(Deferred::value(321)));
            } catch (\Throwable $e) {
                var_dump($e->getMessage());
            }
        }))->start(10);
    
        var_dump('TASK');
    });

    var_dump('DONE');
});

?>
--EXPECT--
string(4) "TASK"
string(5) "TIMER"
string(65) "Cannot await in the fiber that is running the task scheduler loop"
string(4) "DONE"
string(4) "TASK"
string(5) "TIMER"
string(62) "Cannot await while the task scheduler is not dispatching tasks"
string(4) "DONE"
