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

$scheduler->task(function () {
    var_dump(Context::get('foo'));
    
    Task::asyncWithContext(Context::inherit([
        'foo' => 321
    ]), function () {
        var_dump(Context::get('foo'));
    });
    
    $context = Context::inherit()->with('foo', 'baz');
    
    $context->run(function () {
        var_dump(Context::get('foo'));
    });
    
    $context = Context::inherit()->without('foo')->without('bar');
    
    $context->run(function () {
        var_dump(Context::get('foo'));
    });
});

var_dump(Context::get('foo'));

$scheduler->run();

$scheduler = new TaskScheduler([
    'foo' => 'bar'
]);

$scheduler->task(function () {
    var_dump(Context::get('foo'));
});

$scheduler->run();

?>
--EXPECTF--
NULL
NULL
string(3) "baz"
NULL
int(321)
string(3) "bar"
