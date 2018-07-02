<?php

use Concurrent\Context;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler([
    'num' => 123,
    'foo' => 'bar'
]);

$t = $scheduler->task(function () {
    $context = Context::inherit()->with('num', 777);
    $context = $context->with('num', 888);
    
    var_dump(Context::get('num'));
    
    $context->run(function () {
        var_dump(Context::get('num'));
    });
    
    var_dump(Context::get('num'));
    
    $context = $context->without('num');
    
    $context->run(function () {
        var_dump(Context::get('num'));
    });
    
    var_dump(Context::get('num'));
});

$scheduler->run();
