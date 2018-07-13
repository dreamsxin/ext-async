<?php

/*
 +----------------------------------------------------------------------+
 | PHP Version 7                                                        |
 +----------------------------------------------------------------------+
 | Copyright (c) 1997-2018 The PHP Group                                |
 +----------------------------------------------------------------------+
 | This source file is subject to version 3.01 of the PHP license,      |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.php.net/license/3_01.txt                                  |
 | If you did not receive a copy of the PHP license and are unable to   |
 | obtain it through the world-wide-web, please send a note to          |
 | license@php.net so we can mail you a copy immediately.               |
 +----------------------------------------------------------------------+
 | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
 +----------------------------------------------------------------------+
 */

namespace Concurrent;

/**
 * Resolve when all input awaitables have been resolved.
 * 
 * Requires at least one input awaitable, resolves with an array containing all resolved values. The order and
 * keys of the input array are preserved in the result array.
 * 
 * The combinator will fail if any input awaitable fails.
 */
function all(array $awaitables): Awaitable
{
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

/**
 * Resolves with the value of the first succesful resolution of an input awaitable.
 * 
 * Requires at least one input awaitable, the first resolved value will be forwarded. The combinator
 * will fail if all input awaitables have failed.
 */
function race(array $awaitables): Awaitable
{
    $race = function (Deferred $defer, $last, $k, $e, $v) {
        if ($e === null) {
            $defer->resolve($v);
        } elseif ($last) {
            $defer->fail(new \RuntimeException('None of the awaitables resolved'));
        }
    };
    
    return Deferred::combine($awaitables, $race);
}
