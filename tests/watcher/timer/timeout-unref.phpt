--TEST--
Timeout can be unreferenced.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::asyncWithContext(Context::background(), function () {
    try {
        Task::await(Timer::timeout(2000));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

--EXPECT--
string(32) "Task scheduler has been disposed"
