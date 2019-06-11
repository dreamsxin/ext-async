--TEST--
Channel iterator forwards error during call to next().
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();
$channel->close(new \Error('FOO'));

try {
    var_dump($channel->getIterator()->next());
} catch (ChannelClosedException $e) {
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
string(3) "FOO"
