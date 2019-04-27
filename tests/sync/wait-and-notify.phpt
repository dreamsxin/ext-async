--TEST--
Condition can be awaited and signalled.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

use Concurrent\Sync\Condition;

$cond = new Condition();

$available = 0;
$consumed = 0;

Task::async(function () use ($cond, & $available) {
    $timer = new Timer(50);
    $inc = 0;

    while (true) {
        $timer->awaitTimeout();

        $available += $inc++;
        $cond->signal();
    }
});

var_dump('START');

try {
    while ($consumed < 5) {
        while ($available == 0) {
            $cond->wait();
        }

        var_dump($available);

        $consumed += $available;
        $available = 0;
        
        var_dump($consumed);
    }
} finally {
    $cond->close();
}

var_dump('DONE');

--EXPECT--
string(5) "START"
int(1)
int(1)
int(2)
int(3)
int(3)
int(6)
string(4) "DONE"
