--TEST--
Context is created by scheduler and used as root context.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

var_dump(Context::var('foo'));

$scheduler->run(function () {
    var_dump(Context::var('foo'));
    
    Task::asyncWithContext(Context::inherit([
        'foo' => 321
    ]), function () {
        var_dump(Context::var('foo'));
    });
    
    $context = Context::inherit()->with('foo', 'baz');
    
    $context->run(function () {
        var_dump(Context::var('foo'));
    });
    
    $context = Context::inherit()->without('foo')->without('bar');
    
    $context->run(function () {
        var_dump(Context::var('foo'));
    });
});

$scheduler->runWithContext(Context::inherit([
    'foo' => 'bar'
]), function () {
    var_dump(Context::var('foo'));
});

?>
--EXPECT--
NULL
NULL
string(3) "baz"
NULL
int(321)
string(3) "bar"
