<?php

use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler([
    'num' => 123,
    'foo' => 'bar'
]);

$t = $scheduler->task(function () {});

$scheduler->run();

$t->continueWith(function () {
    throw new \Error('FOO!');
});

var_dump('We are still running!');
