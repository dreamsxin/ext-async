--TEST--
Channel group send into closed channel.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    'A' => ($a = new Channel()),
    'B' => ($b = new Channel())
], 0, true);

$a->close();

var_dump(count($group));

try {
    var_dump($group->send(1));
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
}

var_dump(count($group));

$b->close();

var_dump(count($group));

try {
    var_dump($group->send(2));
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
}

var_dump(count($group));

$group = new ChannelGroup([
    $c = new Channel(),
]);

Task::async(function () use ($c) {
    (new Timer(20))->awaitTimeout();

    $c->close();
});

var_dump(count($group));

try {
    var_dump($group->send(3));
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
}

var_dump(count($group));

--EXPECT--
int(2)
string(23) "Channel has been closed"
int(1)
int(1)
string(23) "Channel has been closed"
int(0)
int(1)
string(23) "Channel has been closed"
int(0)
