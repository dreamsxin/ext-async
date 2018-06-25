<?php

use Concurrent\Task;
use Concurrent\TaskScheduler;

$work = function (int $a, int $b): int {
    return $a + $b;
};

$continuation = function (?\Throwable $e, $v = null): void {
    var_dump('CONTINUE WITH', $e, $v);
};

$scheduler = new TaskScheduler();

$t1 = new Task($work, range(1, 2));
$t2 = new Task($work, []);

$t1->continueWith($continuation);
$t2->continueWith($continuation);

$scheduler->start($t1);
$scheduler->start($t2);

$scheduler->run();
