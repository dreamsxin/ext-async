--TEST--
Task exposed backtrace.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$t = Task::async(function () {
    (new Timer(100))->awaitTimeout();
});

print_r($t->getTrace());

(new Timer(5))->awaitTimeout();

print_r($t->getTrace(\DEBUG_BACKTRACE_PROVIDE_OBJECT, 5));

Task::await($t);

print_r($t->getTrace());

?>
--EXPECTF--
Array
(
)
Array
(
    [0] => Array
        (
            [file] => %s
            [line] => %d
            [function] => awaitTimeout
            [class] => Concurrent\Timer
            [object] => Concurrent\Timer Object
                (
                )

            [type] => ->
            [args] => Array
                (
                )

        )

    [1] => Array
        (
            [file] => %s
            [line] => %d
            [function] => Concurrent\{closure}
            [args] => Array
                (
                )

        )

)
Array
(
)
