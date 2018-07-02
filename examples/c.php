<?php

use Concurrent\Context;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler([
    'num' => 123,
    'foo' => 'bar'
]);

$scheduler->task(function () {
    $lookup = [
        Context::class,
        'get'
    ];
    
    $context = Context::inherit([
        'num' => 777
    ]);
    
    var_dump(Context::get('num'));
    var_dump($context->run($lookup, 'num'), $context->run($lookup, 'foo'));
    var_dump(Context::get('num'));
})->continueWith(function (?\Throwable $e, ?int $v = null): void {
    var_dump('CONTINUE WITH', $e, $v);
});

$scheduler->run();
