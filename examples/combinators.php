<?php

namespace Concurrent;

$scheduler = new TaskScheduler();

function all2(array $awaitables): Awaitable
{
    if (empty($awaitables)) {
        return Deferred::value([]);
    }
    
    foreach ($awaitables as $a) {
        if (!$a instanceof Awaitable) {
            throw new \InvalidArgumentException('All awaited items must be awaitables');
        }
    }
    
    $defer = new Deferred();
    
    $result = \array_fill_keys(\array_keys($awaitables), null);
    $remaining = \count($awaitables);
    
    foreach ($awaitables as $k => $a) {
        Task::async(function () use ($k, $a, $defer, & $result, & $remaining) {
            try {
                $result[$k] = Task::await($a);
                
                if (--$remaining === 0) {
                    $defer->resolve($result);
                }
            } catch (\Throwable $e) {
                $defer->fail($e);
            }
        });
    }
    
    return $defer->awaitable();
}

function all(array $awaitables): Awaitable
{
    if (empty($awaitables)) {
        return Deferred::value([]);
    }
    
    $result = \array_fill_keys(\array_keys($awaitables), null);
    
    $all = function (Deferred $defer, $last, $k, $e, $v) use (& $result) {
        if ($e) {
            $defer->fail($e);
        } else {
            $result[$k] = $v;
            
            if ($last) {
                $defer->resolve($result);
            }
        }
    };
    
    return Deferred::combine($awaitables, $all);
}

function race(array $awaitables): Awaitable
{
    $race = function (Deferred $defer, $last, $k, $e, $v) {
        if ($e) {
            if ($last) {
                $defer->fail(new \Error('None of the awaitables resolved'));
            }
        } else {
            $defer->resolve($v);
        }
    };
    
    return Deferred::combine($awaitables, $race);
}

function any(array $awaitables): Awaitable
{
    if (empty($awaitables)) {
        throw new \InvalidArgumentException('At least one awaitable is required');
    }
    
    foreach ($awaitables as $a) {
        if (!$a instanceof Awaitable) {
            throw new \InvalidArgumentException('All awaited items must be awaitables');
        }
    }
    
    $defer = new Deferred();
    
    foreach ($awaitables as $a) {
        Task::async(function () use ($a, $defer) {
            try {
                $defer->resolve(Task::await($a));
            } catch (\Throwable $e) {
                $defer->fail($e);
            }
        });
    }
    
    return $defer->awaitable();
}

$result = $scheduler->run(function () {
    $t = Task::async(function () {
        return 321;
    });
    
    return Task::await(all([
        'A' => $t,
        'B' => Deferred::value(777)
    ]));
});

var_dump($result);
