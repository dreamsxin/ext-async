<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

$scheduler->run(function () {
    $var = new ContextVar();
    
    var_dump($var->get());
    
    $context = Context::current();
    $context = $context->with($var, 321);
    
    $context->run(function () use ($var) {
        var_dump($var->get());
    });
    
    var_dump($var->get());
    var_dump($var->get($context));
    var_dump($var->get());
});

// $scheduler->runWithContext(Context::inherit([
//     'num' => 123,
//     'foo' => 'bar'
// ]), function () {
//     $context = Context::inherit()->with('num', 777);
//     $context = $context->with('num', 888);
    
//     var_dump(Context::var('num'));
    
//     $context->run(function () {
//         var_dump(Context::var('num'));
//     });
    
//     var_dump(Context::var('num'));
    
//     $context = $context->without('num');
    
//     $context->run(function () {
//         var_dump(Context::var('num'));
//     });
    
//     var_dump(Context::var('num'));
// });
