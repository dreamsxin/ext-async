--TEST--
Channel cannot send message after being closed.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();
$channel->close(new \Error('FOO'));

try {
    $channel->send(1);
} catch (ChannelClosedException $e) {
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
string(3) "FOO"
