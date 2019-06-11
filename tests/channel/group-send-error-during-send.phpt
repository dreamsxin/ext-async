--TEST--
Channel group send error forwarding (during send).
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $a = new Channel(),
    $b = new Channel()
]);

Task::async(function () use ($a, $b) {
    $a->close();
    $b->close(new \Error('BAR'));
});

var_dump(count($group));

try {
    $group->send(0);
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
}

var_dump(count($group));

try {
    $group->send(1);
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

var_dump(count($group));

--EXPECT--
int(2)
string(23) "Channel has been closed"
int(1)
string(23) "Channel has been closed"
string(3) "BAR"
int(0)
