--TEST--
Channel group send can be cancelled.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $c = new Channel()
]);

$context = Context::current()->withTimeout(50);

try {
    $context->run(function () use ($group) {
        var_dump($group->send(123));
    });
} catch (CancellationException $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(21) "Context has timed out"
