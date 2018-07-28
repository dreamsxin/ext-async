--TEST--
Task prevents multiple inlining of the same task instance.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

Task::async(function () use (&$awaitable) {
    $defer = new Deferred();
    
    (new Timer(function () use ($defer) {
        $defer->resolve(123);
    }))->start(50);
    
    $awaitable = Task::async(function () use ($defer) {
        return Task::await($defer->awaitable());
    });
});

Task::async(function () use (&$awaitable) {
    var_dump(Task::await($awaitable));
});

Task::async(function () use (&$awaitable) {
    var_dump(Task::await($awaitable));
});

--EXPECT--
int(123)
int(123)
