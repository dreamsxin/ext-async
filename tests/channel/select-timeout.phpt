--TEST--
Channel select will respect timeout.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    3 => $channel = new Channel()
]);

Task::async(function () use ($channel) {
    (new Timer(80))->awaitTimeout();
    
    $channel->send('DONE');
    $channel->close();
});

var_dump($group->select(50));

$val = $group->select(50);
var_dump($val->key);
var_dump($val->value);

var_dump($group->select(50));

--EXPECT--
NULL
int(3)
string(4) "DONE"
NULL
