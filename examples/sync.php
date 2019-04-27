<?php

namespace Concurrent;

use Concurrent\Sync\Condition;

error_reporting(-1);

$cond = new Condition();

$available = 0;
$consumed = 0;

Task::async(function () use ($cond, & $available) {
    $timer = new Timer(500);

    while (true) {
        $timer->awaitTimeout();

        $available += random_int(0, 2);
        $cond->signal();
    }
});

try {
    while ($consumed < 5) {
        while ($available == 0) {
            var_dump('WAIT');
            $cond->wait();
            var_dump('SIGNALED');
        }

        var_dump($available);

        $consumed += $available;
        $available = 0;
    }
} finally {
    var_dump('DONE');

    $cond->close();
}
