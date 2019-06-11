--TEST--
Timer can be awaited within task.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::async(function () {
    $timer = new Timer(50);

    var_dump('START');

    for ($i = 0; $i < 3; $i++) {
        $timer->awaitTimeout();
    
        var_dump($i);
    }

    var_dump('DONE');
});

--EXPECT--
string(5) "START"
int(0)
int(1)
int(2)
string(4) "DONE"
