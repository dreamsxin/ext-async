<?php




namespace Concurrent;

$scheduler = new TaskScheduler();

function job(Deferred $defer, ContextVar $var)
{
    var_dump('INNER DONE!');
    
    $defer->resolve($num = $var->get());
    
    return $num;
}

$scheduler->run(function () {
    $var = new ContextVar();
    
    $context = Context::current();
    $context = $context->with($var, 777);
    
    $defer = new Deferred();

    $t = Task::asyncWithContext($context, __NAMESPACE__ . '\\job', $defer, $var);
    
    var_dump('GO WAIT');
    var_dump(Task::await($defer->awaitable()));
    var_dump(Task::await($t));
    var_dump('OUTER DONE!');
});
