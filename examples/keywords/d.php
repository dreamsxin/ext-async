<?php

// Example requires installing PHP from patched source (branch name is "async"!):
// https://github.com/concurrent-php/php-src/tree/async

namespace Concurrent;

function job(Deferred $defer, ContextVar $var)
{
    var_dump('INNER DONE!');
    
    $defer->resolve($num = $var->get());
    
    return $num;
}

$var = new ContextVar();

$context = Context::current();
$context = $context->with($var, 777);

$defer = new Deferred();

$t = async $context => job($defer, $var);

var_dump('GO WAIT');
var_dump(await $defer->awaitable());
var_dump(await $t);
var_dump('OUTER DONE!');
