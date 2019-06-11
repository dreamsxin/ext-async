--TEST--
Task can be inlined into another task.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$var = new ContextVar();

$context = Context::current();
$context = $context->with($var, 123);

TaskScheduler::runWithContext($context, function () use ($var) {
    $callback = function () use ($var) {
        return $var->get();
    };

    var_dump($var->get());
    
    var_dump(Task::await(Task::async($callback)));

    var_dump($var->get());
    
    var_dump(Task::await(Task::asyncWithContext(Context::current()->with($var, 777), $callback)));

    var_dump($var->get());
    
    try {
        Task::await(Task::async(function () {
            throw new \Error('FAIL!');
        }));
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

?>
--EXPECT--
int(123)
int(123)
int(123)
int(777)
int(123)
string(5) "FAIL!"
