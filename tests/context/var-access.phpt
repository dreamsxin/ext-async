--TEST--
Context provides access to variables.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$var = new ContextVar();

var_dump($var->get());

TaskScheduler::run(function () use ($var) {
    var_dump($var->get());
    
    Task::await(Task::asyncWithContext(Context::current()->with($var, 321), function (ContextVar $var) {
        var_dump($var->get());
    }, $var));
    
    $context = Context::current()->with($var, 'baz');
    
    $context->run(function () use ($var) {
        var_dump($var->get());
    });
    
    var_dump($var->get());
    var_dump($var->get($context));
    var_dump($var->get());
});

var_dump($var->get());

$context = Context::current();
$context = $context->with($var, 'foo');

TaskScheduler::runWithContext($context, function () use ($var) {
    var_dump($var->get());
});

var_dump($var->get());

?>
--EXPECT--
NULL
NULL
int(321)
string(3) "baz"
NULL
string(3) "baz"
NULL
NULL
string(3) "foo"
NULL
