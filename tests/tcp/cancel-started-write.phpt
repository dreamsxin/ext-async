--TEST--
TCP cancel running write operation.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;

list ($a, $b) = TcpSocket::pair();

$context = Context::current()->withTimeout(50);

var_dump('START');

try {
    $context->run(function () use ($a) {
        try {
            $a->write(str_repeat('A', 1024 * 1024 * 8));
        } catch (\Throwable $e) {
            var_dump($e->getMessage());
        } finally {
            var_dump('CLOSE!');
            $a->close();
        }
    });
} finally {
    $b->close();
}

var_dump('DONE');

--EXPECT--
string(5) "START"
string(21) "Context has timed out"
string(6) "CLOSE!"
string(4) "DONE"
