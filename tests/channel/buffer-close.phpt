--TEST--
Channel buffer and close interaction
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel(2);
$group = new ChannelGroup([$channel]);

var_dump($channel->isClosed());

var_dump($group->send('X', 0));
var_dump($group->send('X', 0));
var_dump($group->send('X', 0));

$channel->close();

var_dump($channel->isClosed());

foreach ($channel as $v) {
    var_dump($v);
}

var_dump($group->select());

--EXPECT--
bool(false)
int(0)
int(0)
NULL
bool(true)
string(1) "X"
string(1) "X"
NULL
