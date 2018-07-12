<?php




namespace Concurrent;

$scheduler = new TaskScheduler();

function job(Deferred $defer)
{
    var_dump('INNER DONE!');
    
    $defer->resolve(Context::var('number'));
    return 123;
}

$scheduler->run(function () {
    $context = Context::inherit([
        'number' => 777
    ]);
    
    $defer = new Deferred();

    $t = Task::asyncWithContext($context, __NAMESPACE__ . '\\job', $defer);
    
    var_dump('GO WAIT');
    var_dump(Task::await($defer->awaitable()));
    var_dump(Task::await($t));
    var_dump('OUTER DONE!');
});
