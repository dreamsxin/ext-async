--TEST--
Context variable access uses inheritance.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$var = new ContextVar();

$context = Context::current();
$context = $context->with($var, 'foo');

var_dump($var->get());

TaskScheduler::runWithContext($context, function () use ($var) {
    var_dump($var->get());
    
    $ctx = Context::current()->background()->with($var, 'bar');
    
    var_dump($var->get());
    
    Task::await(Task::asyncWithContext($ctx, function () use ($var) {
        var_dump($var->get());
    }));
    
    var_dump($var->get());
    
    $ctx->run(function (ContextVar $var) {
        var_dump($var->get());
    }, $var);
    
    var_dump($var->get());
});

var_dump($var->get());

?>
--EXPECT--
NULL
string(3) "foo"
string(3) "foo"
string(3) "bar"
string(3) "foo"
string(3) "bar"
string(3) "foo"
NULL
