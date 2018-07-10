<?php

use Concurrent\Context;
use Concurrent\TaskScheduler;

$scheduler = new TaskScheduler();

$scheduler->runWithContext(Context::inherit([
    'num' => 123,
    'foo' => 'bar'
]), function () {
    $context = Context::inherit()->with('num', 777);
    $context = $context->with('num', 888);
    
    var_dump(Context::var('num'));
    
    $context->run(function () {
        var_dump(Context::var('num'));
    });
    
    var_dump(Context::var('num'));
    
    $context = $context->without('num');
    
    $context->run(function () {
        var_dump(Context::var('num'));
    });
    
    var_dump(Context::var('num'));
});
