--TEST--
Deferred transform can deferreds as input.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump(Task::await(Deferred::transform(Deferred::value(123), function ($e, $v) {
    var_dump($e, $v);
})));

var_dump(Task::await(Deferred::transform(Deferred::value(123), function ($e, $v) {
    return $v * 2;
})));

Task::await(Deferred::transform(Deferred::error(new \Error('FOO!')), function (\Throwable $e, $v) {
    var_dump($e->getMessage(), $v);
}));

try {
    Task::await(Deferred::transform(Deferred::error(new \Error('FOO!')), function (\Throwable $e, $v) {
        throw new \Error('BAR!', 0, $e);
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage(), $e->getPrevious()->getMessage());
}

try {
    Task::await(Deferred::transform(Deferred::value(123), function () {
        Task::await(Deferred::value(321));
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async(function () {
        TaskScheduler::run(function () {
            Task::await(Deferred::transform(Deferred::value(123), function () {
                Task::await(Deferred::value(321));
            }));
        });
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

Deferred::transform(Deferred::value(123), function () {
    var_dump('END');
    exit();
});

--EXPECT--
NULL
int(123)
NULL
int(246)
string(4) "FOO!"
NULL
string(4) "BAR!"
string(4) "FOO!"
string(41) "Cannot await within the current execution"
string(41) "Cannot await within the current execution"
string(3) "END"
