<?php

// Example requires installing PHP from patched source (branch name is "async"!):
// https://github.com/concurrent-php/php-src/tree/async

namespace Concurrent;

$scheduler = new TaskScheduler();

function job(Deferred $defer)
{
    var_dump('INNER DONE!');
    
    $defer->resolve(Context::var('number'));
    return 123;
}

$scheduler->task(function () {    
    $context = Context::inherit([
        'number' => 777
    ]);
    
    $defer = new Deferred();

    $t = async $context => job($defer);
    
    var_dump('GO WAIT');
    var_dump(await $defer->awaitable());
    var_dump(await $t);
    var_dump('OUTER DONE!');
});

$scheduler->run();
