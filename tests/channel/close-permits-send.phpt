--TEST--
Channel close prevents further send operations
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();
$channel->close();

var_dump(iterator_to_array($channel->getIterator()));

try {
    $channel->send('X');
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
}

$channel = new Channel();

Task::async(function () use ($channel) {
    $channel->close(new \Error('FOO!'));
});

try {
    $channel->send('X');
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
}

--EXPECT--
array(0) {
}
string(23) "Channel has been closed"
string(23) "Channel has been closed"
