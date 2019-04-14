<?php

namespace Concurrent;

function all(array $awaitables): Awaitable
{
    $result = \array_fill_keys(\array_keys($awaitables), null);

    $all = function (Deferred $defer, bool $last, $k, ?\Throwable $e, $v = null) use (& $result) {
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
    $race = function (Deferred $defer, bool $last, $k, ?\Throwable $e, $v = null) {
        if ($e) {
            $defer->fail($e);
        } else {
            $defer->resolve($v);
        }
    };

    return Deferred::combine($awaitables, $race);
}
